#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace pcm2he {

// 直接 I 型双二次（biquad）滤波器。
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    double process(double x);
    void reset() { z1 = z2 = 0.0; }
};

// RBJ 带通滤波器系数（恒裙边增益，峰值增益 = Q）。
Biquad makeBandPass(double fs, double f0, double q);

// 级联 [stages] 个 RBJ 带通做带通滤波。
void bandpass(const std::vector<float>& in, std::vector<float>& out, double fs, double low,
              double high, int stages = 2);

// 周期汉宁窗。
std::vector<double> hannWindow(int n);

// 原地基-2 FFT，长度必须是 2 的幂。
void fft(std::vector<std::complex<double>>& a);

// 在 [loHz, hiHz] 内找主频（汉宁窗 + 抛物线插值）。返回 0 表示无有效峰。
double dominantFreq(const float* samples, int n, double fs, double loHz, double hiHz);

// 均方根。
double rms(const float* s, int n);

// 百分位数（会拷贝并排序入参）。
double percentile(std::vector<double> v, double pct);

}  // namespace pcm2he
