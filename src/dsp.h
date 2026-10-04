#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace pcm2he {

// Direct Form I biquad filter.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    double process(double x);
    void reset() { z1 = z2 = 0.0; }
};

// RBJ band-pass coefficients (constant skirt gain, peak gain = Q).
Biquad makeBandPass(double fs, double f0, double q);

// Band-pass filter by cascading [stages] RBJ band-pass biquads.
void bandpass(const std::vector<float>& in, std::vector<float>& out, double fs, double low,
              double high, int stages = 2);

// Periodic Hann window.
std::vector<double> hannWindow(int n);

// In-place radix-2 FFT; length must be a power of two.
void fft(std::vector<std::complex<double>>& a);

// Dominant frequency within [loHz, hiHz] (Hann window + parabolic interpolation).
// Returns 0 when there is no significant peak.
double dominantFreq(const float* samples, int n, double fs, double loHz, double hiHz);

// Root mean square.
double rms(const float* s, int n);

// Percentile (copies and sorts the input).
double percentile(std::vector<double> v, double pct);

}  // namespace pcm2he
