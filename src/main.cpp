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
        "PCM2He - convert PCM audio into RichTap HE haptic effect files\n"
        "\n"
        "Usage:\n"
        "  pcm2he [options] <input-audio>\n"
        "\n"
        "Input:\n"
        "  Parsed as WAV by default (PCM 8/16/24/32-bit and IEEE float).\n"
        "  For raw PCM use --raw together with --format/--rate/--channels.\n"
        "\n"
        "Options:\n"
        "  -o, --outdir DIR        Output directory (default ./out)\n"
        "      --version 1|2       Output HE version, 1=Pattern, 2=PatternList (default 1)\n"
        "      --event-ms MS       Duration of each event (default 100)\n"
        "      --events-per-chunk N  Regular events per file, 1..16 (default 8)\n"
        "      --analysis-rate HZ  Analysis/resample target rate (default 48000)\n"
        "      --low HZ            Band-pass lower bound (default 40)\n"
        "      --high HZ           Band-pass upper bound (default 400)\n"
        "      --gamma X           Envelope compression exponent (default 0.6)\n"
        "      --inverse-law       Invert the engine power law + frequency compensation\n"
        "      --silence X         Zero the envelope below X*reference (default 0, off)\n"
        "      --accent-ms MS      Onset-accent pulse duration (default 5, 0=off)\n"
        "      --burst-ms MS       Short-transient pulse duration (default 8, 0=off)\n"
        "      --pulse-hop-ms MS   Pulse detection frame interval (default 10)\n"
        "      --raw               Input is raw PCM\n"
        "      --format FMT        Raw PCM format: s16|s24|s32|f32 (default f32)\n"
        "      --rate HZ           Raw PCM sample rate (default 48000)\n"
        "      --channels N        Raw PCM channel count (default 1)\n"
        "  -h, --help              Show this help\n");
}

bool needValue(int i, int argc, const char* opt) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "error: option %s requires a value\n", opt);
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
                std::fprintf(stderr, "error: unknown --format %s\n", f.c_str());
                return 2;
            }
        } else if (a == "--rate") {
            ro.rawRate = std::atoi(next("--rate"));
        } else if (a == "--channels") {
            ro.rawChannels = std::atoi(next("--channels"));
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "error: unknown option %s\n", a.c_str());
            return 2;
        } else if (input.empty()) {
            input = a;
        } else {
            std::fprintf(stderr, "error: unexpected positional argument %s\n", a.c_str());
            return 2;
        }
    }

    if (input.empty()) {
        std::fprintf(stderr, "error: no input audio file specified\n");
        return 2;
    }

    pcm2he::AudioBuffer audio;
    std::string err;
    if (!pcm2he::readAudio(input, ro, audio, err)) {
        std::fprintf(stderr, "read failed: %s\n", err.c_str());
        return 1;
    }

    std::printf("input: %s (%d Hz, %d ch, %.2f s)\n", input.c_str(), audio.sampleRate,
                audio.channels, static_cast<double>(audio.frames()) / audio.sampleRate);

    pcm2he::EncodeStats stats;
    if (!pcm2he::encodeToFiles(audio, outDir, eo, stats, err)) {
        std::fprintf(stderr, "encode failed: %s\n", err.c_str());
        return 1;
    }

    std::printf("output: %s  HE %d.0  files %d  events %d  duration %.2f s  reference %.4f\n",
                outDir.c_str(), eo.heVersion, stats.chunks, stats.events,
                stats.durationMs / 1000.0, stats.refRms);
    std::printf("pulses: inserted %d  dropped %d (accent=%dms burst=%dms)\n", stats.pulses,
                stats.pulsesDropped, eo.accentMs, eo.burstMs);
    if (stats.pulsesDropped > 0) {
        std::fprintf(stderr,
                     "note: %d pulse(s) dropped because the event cap was reached; "
                     "lower --events-per-chunk to free slots\n",
                     stats.pulsesDropped);
    }
    return 0;
}
