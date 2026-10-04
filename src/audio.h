#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pcm2he {

// Sample data is stored interleaved by channel, in the range [-1, 1].
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
    bool raw = false;                    // true = raw PCM (no header)
    RawFormat rawFormat = RawFormat::F32;
    int rawRate = 48000;
    int rawChannels = 1;
};

// Read a WAV or raw PCM file. Returns false on failure and sets err.
bool readAudio(const std::string& path, const ReadOptions& opt, AudioBuffer& out,
               std::string& err);

// Interleaved multi-channel -> mono (average).
std::vector<float> toMono(const AudioBuffer& in);

// Linear-interpolation resampling of a mono signal.
std::vector<float> resampleLinear(const std::vector<float>& in, int fromRate, int toRate);

}  // namespace pcm2he
