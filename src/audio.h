#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pcm2he {

// 采样数据按通道交织存放（interleaved），取值范围 [-1, 1]。
struct AudioBuffer {
    std::vector<float> data;
    int sampleRate = 0;
    int channels = 0;

    size_t frames() const {
        return channels > 0 ? data.size() / static_cast<size_t>(channels) : 0;
    }
};

enum class RawFormat { S16, S24, S32, F32 };

struct ReadOptions {
    bool raw = false;                    // true = 裸 PCM（无头部）
    RawFormat rawFormat = RawFormat::F32;
    int rawRate = 48000;
    int rawChannels = 1;
};

// 读取 WAV 或裸 PCM。失败返回 false，err 填错误信息。
bool readAudio(const std::string& path, const ReadOptions& opt, AudioBuffer& out,
               std::string& err);

// 交织多通道 -> 单声道（取平均）。
std::vector<float> toMono(const AudioBuffer& in);

// 线性插值重采样单声道信号。
std::vector<float> resampleLinear(const std::vector<float>& in, int fromRate, int toRate);

}  // namespace pcm2he
