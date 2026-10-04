#include "encoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "dsp.h"
#include "richtap.h"

namespace pcm2he {
namespace {

constexpr int POINTS = 4;
constexpr int EDGE_POINT_MS = 2;
constexpr double MAX_OFFSET = 60.0;
constexpr int BASE_MIN = 20;
constexpr int BASE_MAX = 80;

// 真机单条效果的事件数上限。
constexpr int MAX_EVENTS = 16;

// 脉冲检测参数（移植自 GKME PcmHeEncoder）。
constexpr double ONSET_MIN = 0.25;         // 触发音量突增所需的最小幅度
constexpr double ONSET_DELTA = 0.22;       // 触发突增所需的“突然增大”幅度差
constexpr int ACCENT_REFRACTORY_MS = 80;   // 两次突增之间的最小间隔
constexpr double BURST_SILENCE_AMP = 0.04; // 视为静音的幅度上限
constexpr double BURST_MIN_AMP = 0.06;     // 触发短促瞬态所需的最小峰值
constexpr int BURST_MAX_MS = 60;           // 短促瞬态的有效时长上限
constexpr int BURST_GAP_MS = 40;           // 瞬态内部静音容限
constexpr int BURST_REFRACTORY_MS = 40;    // 两次瞬态之间的最小间隔

struct CurvePoint {
    int time = 0;
    double intensity = 0.0;
    double freqOffset = 0.0;
};

struct HeEvent {
    int relativeTime = 0;
    int duration = 0;
    int baseFreq = rt::HE_AT_RESONANCE;
    std::vector<CurvePoint> points;
};

// 数值格式化：整数输出 "x.0"，小数去掉多余尾零。
std::string fmtNum(double v) {
    if (!std::isfinite(v)) return "0.0";
    if (v == std::floor(v) && std::fabs(v) < 1e12) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", v);
        return buf;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s += "0";
    return s;
}

std::string curveJson(const std::vector<CurvePoint>& pts) {
    std::ostringstream os;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i) os << ',';
        const CurvePoint& p = pts[i];
        os << "{\"Frequency\":" << fmtNum(p.freqOffset)
           << ",\"Intensity\":" << fmtNum(std::clamp(p.intensity, 0.0, 1.0))
           << ",\"Time\":" << p.time << '}';
    }
    return os.str();
}

std::string eventJson(const HeEvent& e, int relativeTime) {
    const int dur = std::clamp(e.duration, 1, 5000);
    std::ostringstream os;
    os << "{\"Event\":{\"Type\":\"continuous\",\"RelativeTime\":" << std::max(relativeTime, 0)
       << ",\"Duration\":" << dur << ",\"Parameters\":{\"Frequency\":"
       << std::clamp(e.baseFreq, 0, 100) << ",\"Intensity\":100,\"Curve\":["
       << curveJson(e.points) << "]}}}";
    return os.str();
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// 把一个控制点电平折算成曲线强度。
double levelToCurve(double amp, int he, bool inverseLaw) {
    if (!inverseLaw) return std::clamp(amp, 0.0, 1.0);
    const int effHe = std::clamp(he, 0, 100);
    return rt::amplitudeToCurve(rt::compensateNormalized(amp, effHe));
}

// 一条额外插入的满强度短事件（时间以整段音频的全局毫秒计）。
struct Pulse {
    int timeMs = 0;
    int durationMs = 0;
    double intensity = 1.0;
};

// 在带通信号的归一化逐帧幅度上检测两类脉冲（音量突增 / 短促瞬态）。
// 移植自 GKME PcmHeEncoder.detectAccent / detectBurst。
std::vector<Pulse> detectPulses(const std::vector<float>& bp, int rate, double ref, int hopMs,
                                int accentMs, int burstMs) {
    std::vector<Pulse> pulses;
    if ((accentMs <= 0 && burstMs <= 0) || ref <= 0.0 || bp.empty()) return pulses;

    const int hop = std::max(1, static_cast<int>(std::llround(rate * hopMs / 1000.0)));
    const int total = static_cast<int>(bp.size());

    double prevAmp = 0.0;
    long long lastAccent = -1000000;

    bool inBurst = false;
    bool armed = true;
    long long burstStart = 0;
    long long burstLastActive = 0;
    long long lastBurst = -1000000;
    int burstStartTime = 0;
    double burstPeak = 0.0;

    auto registerBurst = [&](long long nowMs) {
        const bool isShort = burstPeak >= BURST_MIN_AMP && (burstLastActive - burstStart) <= BURST_MAX_MS;
        if (!isShort) return;
        if (nowMs - lastBurst < BURST_REFRACTORY_MS) return;
        lastBurst = nowMs;
        pulses.push_back(Pulse{burstStartTime, burstMs, 1.0});
    };

    for (int off = 0; off < total; off += hop) {
        const int len = std::min(hop, total - off);
        const double amp = std::clamp(rms(bp.data() + off, len) / ref, 0.0, 1.0);
        const long long timeMs = static_cast<long long>(off) * 1000 / rate;

        if (accentMs > 0) {
            const double rising = amp - prevAmp;
            if (rising >= ONSET_DELTA && amp >= ONSET_MIN &&
                (timeMs - lastAccent) >= ACCENT_REFRACTORY_MS) {
                pulses.push_back(Pulse{static_cast<int>(timeMs), accentMs, 1.0});
                lastAccent = timeMs;
            }
        }

        if (burstMs > 0) {
            if (amp >= BURST_MIN_AMP) {
                if (!inBurst) {
                    if (armed) {
                        inBurst = true;
                        burstStart = timeMs;
                        burstStartTime = static_cast<int>(timeMs);
                        burstLastActive = timeMs;
                        burstPeak = amp;
                    }
                } else {
                    if (amp > burstPeak) burstPeak = amp;
                    burstLastActive = timeMs;
                    if (burstLastActive - burstStart > BURST_MAX_MS) {
                        inBurst = false;
                        armed = false;
                    }
                }
            } else if (inBurst) {
                if (burstLastActive - burstStart > BURST_MAX_MS) {
                    inBurst = false;
                    armed = false;
                } else if (timeMs - burstLastActive >= BURST_GAP_MS) {
                    inBurst = false;
                    registerBurst(timeMs);
                    armed = true;
                }
            } else if (amp < BURST_SILENCE_AMP) {
                armed = true;
            }
        }
        prevAmp = amp;
    }
    if (burstMs > 0 && inBurst) {
        inBurst = false;
        const long long nowMs = static_cast<long long>(total) * 1000 / rate;
        registerBurst(nowMs);
    }
    return pulses;
}

// 把一条脉冲转成满强度短事件（控制点同样聚到两端）。
HeEvent makePulseEvent(const Pulse& pulse, int relativeTimeMs) {
    const int dur = std::max(pulse.durationMs, 1);
    const int edge = std::clamp(EDGE_POINT_MS, 1, std::max(1, dur / 4));
    const double v = std::clamp(pulse.intensity, 0.0, 1.0);
    HeEvent ev;
    ev.relativeTime = std::max(relativeTimeMs, 0);
    ev.duration = dur;
    ev.baseFreq = rt::HE_AT_RESONANCE;
    ev.points = {
        CurvePoint{0, v, 0.0},
        CurvePoint{edge, v, 0.0},
        CurvePoint{dur - edge, v, 0.0},
        CurvePoint{dur, v, 0.0},
    };
    return ev;
}

}  // namespace

bool encodeToFiles(const AudioBuffer& input, const std::string& outDir,
                   const EncodeOptions& opt, EncodeStats& stats, std::string& err) {
    if (input.channels <= 0 || input.data.empty()) {
        err = "输入音频为空或无有效通道";
        return false;
    }
    if (opt.heVersion != 1 && opt.heVersion != 2) {
        err = "HE 版本仅支持 1 或 2";
        return false;
    }
    if (opt.eventMs <= 0 || opt.eventsPerChunk <= 0 || opt.eventsPerChunk > 16) {
        err = "参数非法：eventMs 需 > 0，eventsPerChunk 需在 1..16";
        return false;
    }

    const int rate = opt.analysisRate > 0 ? opt.analysisRate : 48000;
    std::vector<float> mono = toMono(input);
    if (mono.empty()) {
        err = "输入音频为空";
        return false;
    }
    if (input.sampleRate != rate) mono = resampleLinear(mono, input.sampleRate, rate);
    const int N = static_cast<int>(mono.size());
    if (N < rate / 20) {
        err = "音频过短（不足 50ms）";
        return false;
    }

    std::vector<float> bp;
    bandpass(mono, bp, rate, opt.lowHz, opt.highHz, 2);

    // 参考电平：带通信号的 40ms/5ms 分帧 RMS 的 95 百分位。
    const int refWin = std::max(1, static_cast<int>(rate * 0.04));
    const int refHop = std::max(1, static_cast<int>(rate * 0.005));
    std::vector<double> frameRms;
    frameRms.reserve(static_cast<size_t>(N / refHop) + 1);
    for (int i = 0; i + refWin <= N; i += refHop) {
        frameRms.push_back(rms(bp.data() + i, refWin));
    }
    double ref = frameRms.empty() ? 0.0 : percentile(frameRms, 0.95);
    if (ref < 1e-6) ref = 1e-6;

    // 逐事件提取（幅度, 音高）。
    const int evSamples = std::max(1, static_cast<int>(std::llround(rate * opt.eventMs / 1000.0)));
    const int totalEvents = (N + evSamples - 1) / evSamples;
    const int freqWin = std::min(evSamples, std::max(8, static_cast<int>(rate * 0.04)));

    std::vector<double> ampLevel(totalEvents, 0.0);
    std::vector<int> heLevel(totalEvents, rt::HE_AT_RESONANCE);
    for (int e = 0; e < totalEvents; ++e) {
        const int start = e * evSamples;
        const int end = std::min(start + evSamples, N);
        const int len = end - start;
        double a01 = rms(bp.data() + start, len) / ref;
        a01 = std::clamp(a01, 0.0, 1.0);
        if (opt.silenceRatio > 0.0 && a01 < opt.silenceRatio) a01 = 0.0;
        ampLevel[e] = std::pow(a01, std::max(opt.gamma, 1e-3));

        const int center = start + len / 2;
        int wStart = std::clamp(center - freqWin / 2, 0, std::max(0, N - freqWin));
        const double freq =
            dominantFreq(mono.data() + wStart, std::min(freqWin, N - wStart), rate, opt.lowHz,
                         opt.highHz);
        heLevel[e] = freq > 0.0 ? rt::hzToHe(rt::shiftIntoRange(freq)) : rt::HE_AT_RESONANCE;
    }

    // 脉冲检测（音量突增 / 短促瞬态）。
    const std::vector<Pulse> pulses =
        detectPulses(bp, rate, ref, opt.pulseHopMs, opt.accentMs, opt.burstMs);
    size_t pulseIdx = 0;
    int pulsesAdded = 0;
    int pulsesDropped = 0;

    // 组装分块。
    std::vector<std::vector<HeEvent>> chunks;
    double seamAmp = 0.0;
    int seamHe = rt::HE_AT_RESONANCE;
    for (int c0 = 0; c0 < totalEvents; c0 += opt.eventsPerChunk) {
        std::vector<HeEvent> chunk;
        const int cEnd = std::min(c0 + opt.eventsPerChunk, totalEvents);
        for (int e = c0; e < cEnd; ++e) {
            const int base = std::clamp(heLevel[e], BASE_MIN, BASE_MAX);
            const int offStart = static_cast<int>(std::llround(
                std::clamp(static_cast<double>(seamHe - base), -MAX_OFFSET, MAX_OFFSET)));
            const int offEnd = static_cast<int>(std::llround(
                std::clamp(static_cast<double>(heLevel[e] - base), -MAX_OFFSET, MAX_OFFSET)));
            const int effHeStart = std::clamp(base + offStart, 0, 100);
            const int effHeEnd = std::clamp(base + offEnd, 0, 100);

            const int edge = std::clamp(EDGE_POINT_MS, 1, std::max(1, opt.eventMs / 4));
            const double iStart = levelToCurve(seamAmp, effHeStart, opt.inverseLaw);
            const double iEnd = levelToCurve(ampLevel[e], effHeEnd, opt.inverseLaw);

            HeEvent ev;
            ev.relativeTime = (e - c0) * opt.eventMs;
            ev.duration = opt.eventMs;
            ev.baseFreq = base;
            ev.points = {
                CurvePoint{0, iStart, static_cast<double>(offStart)},
                CurvePoint{edge, iStart, static_cast<double>(offStart)},
                CurvePoint{opt.eventMs - edge, iEnd, static_cast<double>(offEnd)},
                CurvePoint{opt.eventMs, iEnd, static_cast<double>(offEnd)},
            };
            chunk.push_back(std::move(ev));

            seamAmp = ampLevel[e];
            seamHe = heLevel[e];
        }

        // 把落在本分块时间窗内的脉冲并入（与常规事件可重叠），受事件数上限约束。
        const int chunkStartMs = c0 * opt.eventMs;
        const int chunkEndMs = cEnd * opt.eventMs;
        while (pulseIdx < pulses.size() && pulses[pulseIdx].timeMs < chunkStartMs) ++pulseIdx;
        while (pulseIdx < pulses.size() && pulses[pulseIdx].timeMs < chunkEndMs) {
            const Pulse& p = pulses[pulseIdx];
            if (static_cast<int>(chunk.size()) < MAX_EVENTS) {
                chunk.push_back(makePulseEvent(p, p.timeMs - chunkStartMs));
                ++pulsesAdded;
            } else {
                ++pulsesDropped;
            }
            ++pulseIdx;
        }
        std::sort(chunk.begin(), chunk.end(),
                  [](const HeEvent& a, const HeEvent& b) { return a.relativeTime < b.relativeTime; });
        chunks.push_back(std::move(chunk));
    }

    // 裁剪最后一个分块尾部的纯静音事件。
    if (!chunks.empty()) {
        auto& last = chunks.back();
        while (!last.empty()) {
            bool silent = true;
            for (const auto& p : last.back().points) {
                if (p.intensity > 1e-9) {
                    silent = false;
                    break;
                }
            }
            if (!silent) break;
            last.pop_back();
        }
        if (last.empty() && chunks.size() > 1) chunks.pop_back();
    }

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec && !std::filesystem::exists(outDir)) {
        err = "无法创建输出目录: " + outDir + " (" + ec.message() + ")";
        return false;
    }

    std::vector<std::string> names;
    int eventCount = 0;
    for (size_t i = 0; i < chunks.size(); ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "chunk_%03zu.json", i);
        const std::string fname(name);
        names.emplace_back(fname);

        std::ostringstream os;
        os << "{\"Metadata\":{\"Created\":\"pcm2he\",\"Description\":\"chunk "
           << i << "\",\"Version\":" << opt.heVersion << "},";
        if (opt.heVersion == 1) {
            os << "\"Pattern\":[";
            for (size_t k = 0; k < chunks[i].size(); ++k) {
                if (k) os << ',';
                os << eventJson(chunks[i][k], chunks[i][k].relativeTime);
            }
            os << "]}";
        } else {
            // HE 2.0：每个事件一个 PatternList 项，AbsoluteTime 用事件起始时间。
            os << "\"PatternList\":[";
            for (size_t k = 0; k < chunks[i].size(); ++k) {
                if (k) os << ',';
                os << "{\"AbsoluteTime\":" << chunks[i][k].relativeTime << ",\"Pattern\":["
                   << eventJson(chunks[i][k], 0) << "]}";
            }
            os << "]}";
        }
        writeText(std::filesystem::path(outDir) / fname, os.str());
        eventCount += static_cast<int>(chunks[i].size());
    }

    std::ostringstream man;
    man << "{\"event_ms\":" << opt.eventMs << ",\"events_per_chunk\":" << opt.eventsPerChunk
        << ",\"chunk_ms\":" << (opt.eventMs * opt.eventsPerChunk)
        << ",\"he_version\":" << opt.heVersion << ",\"sample_rate\":" << rate
        << ",\"chunks\":[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) man << ',';
        man << '"' << names[i] << '"';
    }
    man << "]}";
    writeText(std::filesystem::path(outDir) / "manifest.json", man.str());

    stats.chunks = static_cast<int>(chunks.size());
    stats.events = eventCount;
    stats.sampleRate = rate;
    stats.durationMs = 1000.0 * N / rate;
    stats.refRms = ref;
    stats.pulses = pulsesAdded;
    stats.pulsesDropped = pulsesDropped;
    return true;
}

}  // namespace pcm2he
