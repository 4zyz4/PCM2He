#pragma once

// RichTap HE engine tables and amplitude/frequency model, ported from GKME:
//   android/src/main/java/com/zyz4/gkme/haptic/RichTapEngine.kt
//   android/src/main/java/com/zyz4/gkme/haptic/RichTapFrequency.kt
//
// Calibrated on a real device (Xiaomi manet / Snapdragon 8 Gen 3);
// see GKME docs/richtap-hd-vibration.md.

namespace pcm2he {
namespace rt {

// HE Frequency at resonance (engine default, measured ~169 Hz).
constexpr int HE_AT_RESONANCE = 56;

// Resonance frequency (Hz), used as f0 for amplitude-frequency compensation.
constexpr double RESONANCE_HZ = 169.1;

// Mechanical quality factor Q (can be calibrated on a real device).
constexpr double MECHANICAL_Q = 10.0;

// Upper bound of the frequency compensation gain.
constexpr double MAX_FREQ_COMPENSATION = 2.0;

// HE Frequency (0..100) -> measured mechanical frequency (Hz).
double heToHz(int he);

// Desired frequency (Hz) -> HE Frequency (0..100).
int hzToHe(double hz);

// Frequency range the engine can reproduce (Hz).
double minHz();
double maxHz();

// Shift a frequency into the motor's usable range: frequencies that are too low are moved
// up by octaves, those that are too high are clamped to the maximum.
double shiftIntoRange(double hz);

// Curve intensity c (0..1) -> normalized drive amplitude (0..1): a = c * 1.14^(10(c-1)).
double curveToAmplitude(double c);

// Normalized drive amplitude a (0..1) -> curve intensity c (0..1), the inverse of
// curveToAmplitude (solved by bisection).
double amplitudeToCurve(double a);

// Normalized LRA displacement response (relative to resonance); 1.0 at resonance.
double resonanceResponse(int he, double q = MECHANICAL_Q);

// Amplitude-frequency compensation gain: 1 / resonanceResponse; 1 at resonance, > 1 off it.
double frequencyCompensation(int he, double q = MECHANICAL_Q,
                             double maxBoost = MAX_FREQ_COMPENSATION);

// Apply frequency compensation to a normalized target amplitude.
double compensateNormalized(double a, int he, double q = MECHANICAL_Q,
                            double maxBoost = MAX_FREQ_COMPENSATION);

}  // namespace rt
}  // namespace pcm2he
