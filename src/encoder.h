#pragma once

#include <string>

#include "audio.h"

namespace pcm2he {

struct EncodeOptions {
    // Output HE version: 1 = top-level Pattern; 2 = top-level PatternList.
    int heVersion = 1;

    // Duration of each continuous event (ms); also the time resolution of the envelope.
    int eventMs = 100;

    // Number of regular events per effect (per output file). The engine total cap is 16;
    // the default leaves slots for pulse events. Increase for smoother output at the
    // expense of pulse capacity.
    int eventsPerChunk = 8;

    // Analysis band-pass range (Hz).
    double lowHz = 40.0;
    double highHz = 400.0;

    // Envelope compression exponent: intensity = (rms/ref)^gamma.
    double gamma = 0.6;

    // false = write the normalized envelope directly (matches pcm_to_he.py);
    // true  = apply frequency compensation then invert the engine power law
    //         (matches GKME PcmHeEncoder).
    bool inverseLaw = false;

    // Analysis sample rate (input is resampled to this value).
    int analysisRate = 48000;

    // Force the envelope to zero below silenceRatio * ref (0 = disabled).
    double silenceRatio = 0.0;

    // Onset accent: when the amplitude jumps up, additionally insert a full-strength short
    // event of accentMs. 0 = disabled.
    int accentMs = 5;

    // Short transient response: detect "silence -> short sound -> silence" transients and
    // additionally insert a full-strength short event of burstMs. 0 = disabled.
    int burstMs = 8;

    // Frame interval for pulse detection (ms); should be close to the real audio callback
    // period.
    int pulseHopMs = 10;
};

struct EncodeStats {
    int chunks = 0;
    int events = 0;
    int sampleRate = 0;
    double durationMs = 0.0;
    double refRms = 0.0;
    int pulses = 0;         // pulse events successfully inserted
    int pulsesDropped = 0;  // pulses dropped because the event cap was reached
};

// Encode the decoded audio into multiple HE files under outDir and write manifest.json.
bool encodeToFiles(const AudioBuffer& input, const std::string& outDir,
                   const EncodeOptions& opt, EncodeStats& stats, std::string& err);

}  // namespace pcm2he
