#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "audio.h"
#include "encoder.h"

namespace {

void printUsage() {
    std::printf(
        "PCM2He - 把 PCM 音频转换为 RichTap HE 震动效果文件\n"
        "\n"
        "用法:\n"
        "  pcm2he [选项] <输入音频>\n"
        "\n"
        "输入:\n"
        "  默认按 WAV 解析（支持 PCM 8/16/24/32-bit 与 IEEE float）。\n"
        "  裸 PCM 用 --raw 指定，并给出 --format/--rate/--channels。\n"
        "\n"
        "选项:\n"
        "  -o, --outdir DIR        输出目录（默认 ./out）\n"
        "      --version 1|2       输出 HE 版本，1=Pattern，2=PatternList（默认 1）\n"
        "      --event-ms MS       每个事件时长（默认 100）\n"
        "      --events-per-chunk N  每个文件的事件数，1..16（默认 16）\n"
        "      --analysis-rate HZ  分析/重采样目标率（默认 48000）\n"
        "      --low HZ            带通下限（默认 40）\n"
        "      --high HZ           带通上限（默认 400）\n"
        "      --gamma X           包络压缩指数（默认 0.6）\n"
        "      --inverse-law       使用引擎次方律逆变换 + 频率补偿\n"
        "      --silence X         低于 X*参考电平的包络置零（默认 0，关闭）\n"
        "      --accent-ms MS      音量突增强调脉冲时长（默认 5，0=关闭）\n"
        "      --burst-ms MS       短促瞬态响应脉冲时长（默认 8，0=关闭）\n"
        "      --pulse-hop-ms MS   脉冲检测帧间隔（默认 10）\n"
        "      --raw               输入为裸 PCM\n"
        "      --format FMT        裸 PCM 格式: s16|s24|s32|f32（默认 f32）\n"
        "      --rate HZ           裸 PCM 采样率（默认 48000）\n"
        "      --channels N        裸 PCM 通道数（默认 1）\n"
        "  -h, --help              显示本帮助\n");
}

bool needValue(int i, int argc, const char* opt) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "错误: 选项 %s 缺少参数\n", opt);
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    pcm2he::ReadOptions ro;
    pcm2he::EncodeOptions eo;
    std::string input;
    std::string outDir = "out";

    if (argc < 2) {
        printUsage();
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (!needValue(i, argc, name)) std::exit(2);
            return argv[++i];
        };

        if (a == "-h" || a == "--help") {
            printUsage();
            return 0;
        } else if (a == "-o" || a == "--outdir") {
            outDir = next("--outdir");
        } else if (a == "--version") {
            eo.heVersion = std::atoi(next("--version"));
        } else if (a == "--event-ms") {
            eo.eventMs = std::atoi(next("--event-ms"));
        } else if (a == "--events-per-chunk") {
            eo.eventsPerChunk = std::atoi(next("--events-per-chunk"));
        } else if (a == "--analysis-rate") {
            eo.analysisRate = std::atoi(next("--analysis-rate"));
        } else if (a == "--low") {
            eo.lowHz = std::atof(next("--low"));
        } else if (a == "--high") {
            eo.highHz = std::atof(next("--high"));
        } else if (a == "--gamma") {
            eo.gamma = std::atof(next("--gamma"));
        } else if (a == "--inverse-law") {
            eo.inverseLaw = true;
        } else if (a == "--silence") {
            eo.silenceRatio = std::atof(next("--silence"));
        } else if (a == "--accent-ms") {
            eo.accentMs = std::atoi(next("--accent-ms"));
        } else if (a == "--burst-ms") {
            eo.burstMs = std::atoi(next("--burst-ms"));
        } else if (a == "--pulse-hop-ms") {
            eo.pulseHopMs = std::atoi(next("--pulse-hop-ms"));
        } else if (a == "--raw") {
            ro.raw = true;
        } else if (a == "--format") {
            const std::string f = next("--format");
            if (f == "s16") {
                ro.rawFormat = pcm2he::RawFormat::S16;
            } else if (f == "s24") {
                ro.rawFormat = pcm2he::RawFormat::S24;
            } else if (f == "s32") {
                ro.rawFormat = pcm2he::RawFormat::S32;
            } else if (f == "f32") {
                ro.rawFormat = pcm2he::RawFormat::F32;
            } else {
                std::fprintf(stderr, "错误: 未知 --format %s\n", f.c_str());
                return 2;
            }
        } else if (a == "--rate") {
            ro.rawRate = std::atoi(next("--rate"));
        } else if (a == "--channels") {
            ro.rawChannels = std::atoi(next("--channels"));
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "错误: 未知选项 %s\n", a.c_str());
            return 2;
        } else if (input.empty()) {
            input = a;
        } else {
            std::fprintf(stderr, "错误: 多余的位置参数 %s\n", a.c_str());
            return 2;
        }
    }

    if (input.empty()) {
        std::fprintf(stderr, "错误: 未指定输入音频文件\n");
        return 2;
    }

    pcm2he::AudioBuffer audio;
    std::string err;
    if (!pcm2he::readAudio(input, ro, audio, err)) {
        std::fprintf(stderr, "读取失败: %s\n", err.c_str());
        return 1;
    }

    std::printf("输入: %s (%d Hz, %d ch, %.2f s)\n", input.c_str(), audio.sampleRate,
                audio.channels, static_cast<double>(audio.frames()) / audio.sampleRate);

    pcm2he::EncodeStats stats;
    if (!pcm2he::encodeToFiles(audio, outDir, eo, stats, err)) {
        std::fprintf(stderr, "编码失败: %s\n", err.c_str());
        return 1;
    }

    std::printf("输出: %s  HE %d.0  文件 %d  事件 %d  时长 %.2f s  参考电平 %.4f\n",
                outDir.c_str(), eo.heVersion, stats.chunks, stats.events,
                stats.durationMs / 1000.0, stats.refRms);
    std::printf("脉冲: 插入 %d  丢弃 %d（accent=%dms burst=%dms）\n", stats.pulses,
                stats.pulsesDropped, eo.accentMs, eo.burstMs);
    if (stats.pulsesDropped > 0) {
        std::fprintf(stderr,
                     "提示: 有 %d 条脉冲因分块事件数达上限而被丢弃，可减小 --events-per-chunk 留出槽位\n",
                     stats.pulsesDropped);
    }
    return 0;
}
