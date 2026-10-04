# PCM2He

Offline converter that turns **PCM audio** into **RichTap HE haptic effect files**
(HE 1.0 or HE 2.0). Each output file is a standalone HE effect, and a `manifest.json`
describes the chunking.

The algorithm and device calibration are ported from the HD vibration implementation of
[GKME](https://github.com/4zyz4/GKME)
(`android/src/main/java/com/zyz4/gkme/haptic/`), with reference to its
`docs/richtap-hd-vibration.md` and `docs/richtap/scripts/pcm_to_he.py`.

## How it works

The **amplitude envelope** and **in-band dominant frequency** of the PCM are mapped to the
HE `Intensity` / `Frequency` parameters:

1. Read the audio, downmix to mono and resample to the analysis rate (48kHz by default).
2. Band-pass 40-400Hz; use the 95th percentile of 40ms/5ms framed RMS as the reference level.
3. Extract `(envelope, dominant frequency)` per segment of `eventMs` (100ms by default).
4. Map the frequency through `shiftIntoRange` (octave-up when too low, clamp when too high)
   and look it up in the real-device calibration table to get the HE value.
5. Write each segment as one `continuous` event whose curve has exactly **4 control points**
   **clustered at both ends** (`0, ε, D-ε, D`); adjacent events share the boundary level so
   concatenated files play back without gaps.
6. Additionally detect two kinds of **pulses** and merge them into the owning chunk as
   full-strength short events (they may overlap regular events):
   - **Onset accent** (`--accent-ms`): insert a short pulse when the amplitude jumps up;
   - **Short transient** (`--burst-ms`): detect "silence -> short sound -> silence" events ≤60ms.
7. Split into multiple HE files by the number of regular events per effect (default 8);
   pulses occupy the remaining event slots.

Real-device hard constraints (see the GKME docs): at most 16 events per effect, exactly 4
curve points per event, and at most 5000ms per event.

## Build

Requires g++ (C++17). On Windows:

```powershell
.\build.ps1            # produces build\pcm2he.exe
.\build.ps1 -Clean     # clean rebuild
```

Or with CMake:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Usage

```
pcm2he [options] <input-audio>
```

```powershell
# WAV -> HE 1.0
.\build\pcm2he.exe -o out sample.wav

# WAV -> HE 2.0, 4 events per effect
.\build\pcm2he.exe --version 2 --events-per-chunk 4 -o out sample.wav

# Raw PCM (float32 mono 48k)
.\build\pcm2he.exe --raw --format f32 --rate 48000 --channels 1 -o out src.f32

# Invert the engine power law + frequency compensation (closer to linear displacement)
.\build\pcm2he.exe --inverse-law -o out sample.wav
```

Main options:

| Option | Description | Default |
|--------|-------------|---------|
| `-o, --outdir DIR` | Output directory | `./out` |
| `--version 1\|2` | Output HE version | `1` |
| `--event-ms MS` | Duration of each event (time resolution) | `100` |
| `--events-per-chunk N` | **Regular** events per effect/file (1..16) | `8` |
| `--analysis-rate HZ` | Analysis/resample target rate | `48000` |
| `--low HZ` / `--high HZ` | Analysis band-pass range | `40` / `400` |
| `--gamma X` | Envelope compression exponent | `0.6` |
| `--inverse-law` | Frequency compensation + inverted engine power law | off |
| `--silence X` | Zero the envelope below `X × reference` | `0` (off) |
| `--accent-ms MS` | Onset-accent pulse duration | `5` (`0`=off) |
| `--burst-ms MS` | Short-transient pulse duration | `8` (`0`=off) |
| `--pulse-hop-ms MS` | Pulse detection frame interval | `10` |
| `--raw` / `--format` / `--rate` / `--channels` | Raw PCM input parameters | `f32` / `48000` / `1` |

### Pulses and event slots

A single HE effect holds **at most 16 events** (a real-device hard constraint). Regular events
occupy `--events-per-chunk` slots and the rest are left for pulses. Therefore:

- If pulse-heavy material reports `dropped` pulses, **lower** `--events-per-chunk`
  (e.g. `2`~`4`) to free up space;
- To disable pulses use `--accent-ms 0 --burst-ms 0`, and `--events-per-chunk` can go back to `16`;
- `--pulse-hop-ms` should be close to the real audio callback period (default 10ms); tweak it
  only if pulses are misdetected or missed.

## Output

```
out/
  chunk_000.json   # one HE effect
  chunk_001.json
  ...
  manifest.json    # chunk metadata
```

- `--version 1`: top level is `Pattern`;
- `--version 2`: top level is `PatternList`, one item per event, with `AbsoluteTime` as the
  event start time;
- `manifest.json`: `event_ms`, `events_per_chunk`, `chunk_ms`, `he_version`, `sample_rate`, `chunks`.

## Project layout

```
src/
  main.cpp        CLI entry point
  audio.h/.cpp    WAV / raw PCM reading, downmix, resample
  dsp.h/.cpp      biquad band-pass, FFT, envelope/dominant-frequency features
  richtap.h/.cpp  HE<->Hz calibration table, amplitude law, frequency compensation
  encoder.h/.cpp  feature extraction, HE 1.0/2.0 assembly and chunked output
```

## Known limitations

- Pulse detection thresholds (gain/duration/interval) reuse GKME's real-device calibration
  values, with the frame rate approximated by `--pulse-hop-ms`; results may differ slightly
  from the real device's per-frame streaming implementation.
- Pulse insertion is subject to the 16-event cap; overflow is dropped and reported at runtime.
- The band-pass uses cascaded RBJ biquads instead of a scipy Butterworth filter, so the
  envelope shape differs slightly; after reference-level normalization the impact is limited.
- The mechanical Q for frequency compensation is fixed at `10` and not device-calibrated.

## License

Released under the [MIT License](LICENSE).
Algorithm and frequency calibration data ported from
[GKME](https://github.com/4zyz4/GKME) (Copyright (c) 4zyz4).
