#include "richtap.h"

#include <algorithm>
#include <cmath>

namespace pcm2he {
namespace rt {
namespace {

// 引擎 HE Frequency(0..100) -> 实测机械频率（Hz），索引即 HE。
// 真机加速度计逐点标定（GKME RichTapEngine.HE_HZ）。
const double kHeHz[101] = {
    107.2, 109.0, 109.0, 111.1, 111.1, 113.1, 113.1, 114.9, 114.9, 116.1,
    116.1, 118.0, 118.0, 120.1, 120.1, 121.8, 121.9, 124.1, 124.1, 125.8,
    125.8, 127.8, 128.0, 129.8, 130.0, 131.1, 131.1, 132.8, 132.9, 135.0,
    135.1, 136.8, 136.8, 138.8, 139.1, 141.1, 141.1, 142.8, 142.9, 145.0,
    145.1, 145.8, 146.0, 148.0, 148.1, 149.8, 149.8, 152.0, 152.0, 153.7,
    153.7, 158.9, 159.0, 163.7, 164.0, 169.1, 169.1, 173.8, 173.8, 178.9,
    178.9, 183.8, 183.9, 189.1, 189.1, 193.9, 193.9, 199.0, 199.0, 203.9,
    204.0, 209.0, 209.0, 214.0, 214.0, 220.2, 220.2, 224.8, 224.8, 230.1,
    230.1, 234.9, 235.1, 238.1, 241.1, 244.0, 247.0, 249.9, 249.9, 255.0,
    255.0, 260.0, 260.0, 265.0, 265.0, 270.0, 270.0, 275.1, 275.1, 280.0,
    280.3,
};

constexpr double kLawBase = 1.14;

}  // namespace

double heToHz(int he) {
    const int h = std::clamp(he, 0, 100);
    return kHeHz[h];
}

int hzToHe(double hz) {
    if (std::isnan(hz)) return HE_AT_RESONANCE;
    const double lo = heToHz(0);
    const double hi = heToHz(100);
    const double target = std::clamp(hz, lo, hi);
    int best = 0;
    double bestErr = 1e300;
    int bestDist = 1 << 30;
    for (int h = 0; h <= 100; ++h) {
        const double err = std::fabs(heToHz(h) - target);
        const int dist = std::abs(h - HE_AT_RESONANCE);
        // 表里有重复值（HE 55/56 同为 169.1 等），并列时取最靠近谐振点的那个。
        if (err < bestErr - 1e-9 || (err <= bestErr + 1e-9 && dist < bestDist)) {
            bestErr = err;
            best = h;
            bestDist = dist;
        }
    }
    return best;
}

double minHz() { return heToHz(0); }
double maxHz() { return heToHz(100); }

double shiftIntoRange(double hz) {
    if (!(hz > 0.0) || std::isnan(hz) || std::isinf(hz)) return RESONANCE_HZ;
    double f = hz;
    const double lo = minHz();
    const double hi = maxHz();
    while (f < lo) f *= 2.0;
    return std::clamp(f, lo, hi);
}

double curveToAmplitude(double c) {
    const double cc = std::clamp(c, 0.0, 1.0);
    return cc * std::pow(kLawBase, 10.0 * (cc - 1.0));
}

double amplitudeToCurve(double a) {
    if (a <= 0.0) return 0.0;
    if (a >= 1.0) return 1.0;
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 48; ++i) {
        const double mid = (lo + hi) * 0.5;
        if (curveToAmplitude(mid) < a) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (lo + hi) * 0.5;
}

double resonanceResponse(int he, double q) {
    const double r = heToHz(he) / RESONANCE_HZ;
    const double zeta2 = 1.0 / std::max(q, 1e-3);
    const double denom = std::sqrt(std::pow(1.0 - r * r, 2.0) + std::pow(zeta2 * r, 2.0));
    if (denom <= 1e-12) return 1.0;
    return std::clamp(zeta2 / denom, 0.0, 1.0);
}

double frequencyCompensation(int he, double q, double maxBoost) {
    const double resp = std::max(resonanceResponse(he, q), 1e-3);
    return std::clamp(1.0 / resp, 1.0, std::max(maxBoost, 1.0));
}

double compensateNormalized(double a, int he, double q, double maxBoost) {
    return std::clamp(a * frequencyCompensation(he, q, maxBoost), 0.0, 1.0);
}

}  // namespace rt
}  // namespace pcm2he
