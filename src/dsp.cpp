#include "dsp.h"

#include <algorithm>
#include <cmath>

namespace pcm2he {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

double Biquad::process(double x) {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

Biquad makeBandPass(double fs, double f0, double q) {
    const double w0 = 2.0 * kPi * f0 / fs;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * std::max(q, 1e-6));
    const double a0 = 1.0 + alpha;
    Biquad bq;
    bq.b0 = alpha / a0;
    bq.b1 = 0.0;
    bq.b2 = -alpha / a0;
    bq.a1 = (-2.0 * cw) / a0;
    bq.a2 = (1.0 - alpha) / a0;
    return bq;
}

void bandpass(const std::vector<float>& in, std::vector<float>& out, double fs, double low,
              double high, int stages) {
    if (low <= 0.0 || high <= low || high >= fs * 0.5) {
        out = in;
        return;
    }
    const double f0 = std::sqrt(low * high);
    const double q = f0 / (high - low);
    std::vector<Biquad> cascade(std::max(stages, 1), makeBandPass(fs, f0, q));
    out.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        double x = in[i];
        for (auto& bq : cascade) x = bq.process(x);
        out[i] = static_cast<float>(x);
    }
}

std::vector<double> hannWindow(int n) {
    std::vector<double> w(std::max(n, 1), 1.0);
    if (n <= 1) return w;
    for (int i = 0; i < n; ++i) {
        w[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
    }
    return w;
}

void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    if (n < 2) return;
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

double dominantFreq(const float* samples, int n, double fs, double loHz, double hiHz) {
    if (n < 4) return 0.0;
    size_t N = 1;
    while (N < static_cast<size_t>(n)) N <<= 1;
    const std::vector<double> win = hannWindow(n);
    std::vector<std::complex<double>> buf(N, {0.0, 0.0});
    for (int i = 0; i < n; ++i) buf[i] = std::complex<double>(samples[i] * win[i], 0.0);
    fft(buf);

    const double binHz = fs / static_cast<double>(N);
    int loBin = static_cast<int>(std::ceil(loHz / binHz));
    int hiBin = static_cast<int>(std::floor(hiHz / binHz));
    loBin = std::max(loBin, 1);
    hiBin = std::min(hiBin, static_cast<int>(N / 2) - 1);
    if (hiBin <= loBin) return 0.0;

    int bestBin = -1;
    double bestMag = 0.0;
    double totalMag = 0.0;
    for (int k = loBin; k <= hiBin; ++k) {
        const double mag = std::abs(buf[k]);
        totalMag += mag;
        if (mag > bestMag) {
            bestMag = mag;
            bestBin = k;
        }
    }
    if (bestBin < 0 || totalMag <= 1e-12 || bestMag <= 1e-9) return 0.0;

    double delta = 0.0;
    if (bestBin > 0 && bestBin + 1 < static_cast<int>(N / 2)) {
        const double m0 = std::abs(buf[bestBin - 1]);
        const double m1 = std::abs(buf[bestBin]);
        const double m2 = std::abs(buf[bestBin + 1]);
        const double denom = (m0 - 2.0 * m1 + m2);
        if (std::fabs(denom) > 1e-12) delta = 0.5 * (m0 - m2) / denom;
        delta = std::clamp(delta, -1.0, 1.0);
    }
    return (bestBin + delta) * binHz;
}

double rms(const float* s, int n) {
    if (n <= 0) return 0.0;
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += static_cast<double>(s[i]) * s[i];
    return std::sqrt(acc / n);
}

double percentile(std::vector<double> v, double pct) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double p = std::clamp(pct, 0.0, 1.0) * (v.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(p));
    const size_t hi = static_cast<size_t>(std::ceil(p));
    if (lo == hi) return v[lo];
    const double f = p - lo;
    return v[lo] * (1.0 - f) + v[hi] * f;
}

}  // namespace pcm2he
