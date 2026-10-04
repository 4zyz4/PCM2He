#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

namespace pcm2he {
namespace {

uint16_t rdU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t rdU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool loadFile(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open file: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0) {
        err = "cannot read file size: " + path;
        return false;
    }
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(out.data()), size);
    return true;
}

float decodeSample(const uint8_t* p, int bits, bool isFloat) {
    if (isFloat) {
        if (bits == 32) {
            float v;
            std::memcpy(&v, p, sizeof(float));
            return v;
        }
        if (bits == 64) {
            double v;
            std::memcpy(&v, p, sizeof(double));
            return static_cast<float>(v);
        }
        return 0.0f;
    }
    switch (bits) {
        case 8:  // WAV 8-bit is unsigned
            return (static_cast<int>(p[0]) - 128) / 128.0f;
        case 16: {
            const int16_t v = static_cast<int16_t>(rdU16(p));
            return v / 32768.0f;
        }
        case 24: {
            int32_t v = static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) |
                        (static_cast<int32_t>(p[2]) << 16);
            if (v & 0x00800000) v |= ~0x00FFFFFF;  // sign extension
            return v / 8388608.0f;
        }
        case 32: {
            int32_t v;
            std::memcpy(&v, p, sizeof(int32_t));
            return static_cast<float>(v / 2147483648.0);
        }
        default:
            return 0.0f;
    }
}

bool readWav(const std::vector<uint8_t>& bytes, AudioBuffer& out, std::string& err) {
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        err = "not a valid WAV file (missing RIFF/WAVE)";
        return false;
    }
    int audioFormat = 0;
    int channels = 0;
    int sampleRate = 0;
    int bits = 0;
    size_t dataOff = 0;
    size_t dataLen = 0;

    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const uint8_t* id = bytes.data() + pos;
        const uint32_t csize = rdU32(bytes.data() + pos + 4);
        const size_t body = pos + 8;
        if (std::memcmp(id, "fmt ", 4) == 0 && body + 16 <= bytes.size()) {
            audioFormat = rdU16(bytes.data() + body);
            channels = rdU16(bytes.data() + body + 2);
            sampleRate = static_cast<int>(rdU32(bytes.data() + body + 4));
            bits = rdU16(bytes.data() + body + 14);
            if (audioFormat == 0xFFFE && csize >= 40 && body + 26 <= bytes.size()) {
                // Extensible: the real format code is in the first two bytes of the
                // SubFormat GUID.
                audioFormat = rdU16(bytes.data() + body + 24);
            }
        } else if (std::memcmp(id, "data", 4) == 0) {
            dataOff = body;
            dataLen = std::min<size_t>(csize, bytes.size() - body);
        }
        pos = body + csize + (csize & 1u);  // chunks are word-aligned
    }

    if (channels <= 0 || sampleRate <= 0 || bits <= 0) {
        err = "incomplete WAV header (fmt missing or invalid)";
        return false;
    }
    if (dataOff == 0 || dataLen == 0) {
        err = "WAV has no valid data chunk";
        return false;
    }
    const bool isFloat = (audioFormat == 3);
    if (audioFormat != 1 && audioFormat != 3) {
        err = "unsupported WAV encoding (only integer PCM and IEEE float)";
        return false;
    }
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32 && bits != 64) {
        err = "unsupported bit depth: " + std::to_string(bits);
        return false;
    }
    const int bytesPerSample = bits / 8;
    const size_t total = dataLen / static_cast<size_t>(bytesPerSample);
    out.channels = channels;
    out.sampleRate = sampleRate;
    out.data.resize(total);
    for (size_t i = 0; i < total; ++i) {
        out.data[i] = decodeSample(bytes.data() + dataOff + i * bytesPerSample, bits, isFloat);
    }
    return true;
}

bool readRaw(const std::vector<uint8_t>& bytes, const ReadOptions& opt, AudioBuffer& out,
             std::string& err) {
    int bytesPerSample = 4;
    bool isFloat = false;
    switch (opt.rawFormat) {
        case RawFormat::S16: bytesPerSample = 2; break;
        case RawFormat::S24: bytesPerSample = 3; break;
        case RawFormat::S32: bytesPerSample = 4; break;
        case RawFormat::F32: bytesPerSample = 4; isFloat = true; break;
    }
    const int bits = bytesPerSample * 8;
    if (opt.rawRate <= 0 || opt.rawChannels <= 0) {
        err = "raw PCM requires valid --rate and --channels";
        return false;
    }
    const size_t total = bytes.size() / static_cast<size_t>(bytesPerSample);
    out.channels = opt.rawChannels;
    out.sampleRate = opt.rawRate;
    out.data.resize(total);
    for (size_t i = 0; i < total; ++i) {
        out.data[i] = decodeSample(bytes.data() + i * bytesPerSample, bits, isFloat);
    }
    return true;
}

}  // namespace

bool readAudio(const std::string& path, const ReadOptions& opt, AudioBuffer& out,
               std::string& err) {
    std::vector<uint8_t> bytes;
    if (!loadFile(path, bytes, err)) return false;
    if (bytes.empty()) {
        err = "file is empty: " + path;
        return false;
    }
    if (opt.raw) return readRaw(bytes, opt, out, err);
    return readWav(bytes, out, err);
}

std::vector<float> toMono(const AudioBuffer& in) {
    if (in.channels <= 0) return {};
    const size_t n = in.frames();
    std::vector<float> mono(n, 0.0f);
    if (in.channels == 1) {
        mono = in.data;
        mono.resize(n);
        return mono;
    }
    const float inv = 1.0f / static_cast<float>(in.channels);
    for (size_t i = 0; i < n; ++i) {
        float acc = 0.0f;
        for (int c = 0; c < in.channels; ++c) acc += in.data[i * in.channels + c];
        mono[i] = acc * inv;
    }
    return mono;
}

std::vector<float> resampleLinear(const std::vector<float>& in, int fromRate, int toRate) {
    if (fromRate == toRate || in.empty()) return in;
    const double ratio = static_cast<double>(toRate) / static_cast<double>(fromRate);
    const size_t outN = static_cast<size_t>(std::llround(in.size() * ratio));
    std::vector<float> out(outN, 0.0f);
    const double step = 1.0 / ratio;
    double pos = 0.0;
    for (size_t i = 0; i < outN; ++i) {
        const size_t i0 = static_cast<size_t>(pos);
        const size_t i1 = std::min(i0 + 1, in.size() - 1);
        const double frac = pos - static_cast<double>(i0);
        out[i] = static_cast<float>(in[i0] * (1.0 - frac) + in[i1] * frac);
        pos += step;
    }
    return out;
}

}  // namespace pcm2he
