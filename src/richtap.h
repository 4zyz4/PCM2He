#pragma once

// RichTap HE 引擎换算表与幅度/频率模型，移植自 GKME:
//   android/src/main/java/com/zyz4/gkme/haptic/RichTapEngine.kt
//   android/src/main/java/com/zyz4/gkme/haptic/RichTapFrequency.kt
//
// 真机（Xiaomi manet / Snapdragon 8 Gen 3）标定，见 GKME docs/richtap-hd-vibration.md。

namespace pcm2he {
namespace rt {

// 谐振点对应的 HE Frequency（引擎缺省，实测 ≈169Hz）。
constexpr int HE_AT_RESONANCE = 56;

// 谐振频率（Hz），用于幅度-频率补偿的 f0。
constexpr double RESONANCE_HZ = 169.1;

// 机械品质因数 Q（真机可标定）。
constexpr double MECHANICAL_Q = 10.0;

// 频率补偿增益上限。
constexpr double MAX_FREQ_COMPENSATION = 2.0;

// HE Frequency(0..100) -> 实测机械频率（Hz）。
double heToHz(int he);

// 期望频率（Hz）-> HE Frequency(0..100)。
int hzToHe(double hz);

// 引擎可复现的频率范围（Hz）。
double minHz();
double maxHz();

// 把频率搬移到马达可用范围内：过低按八度上移，过高钳到最大频率。
double shiftIntoRange(double hz);

// 曲线强度 c(0..1) -> 归一化驱动幅度(0..1)：a = c * 1.14^(10(c-1))。
double curveToAmplitude(double c);

// 归一化驱动幅度 a(0..1) -> 曲线强度 c(0..1)，即 curveToAmplitude 的逆（二分求解）。
double amplitudeToCurve(double a);

// LRA 归一化位移响应（相对谐振点），谐振处为 1.0。
double resonanceResponse(int he, double q = MECHANICAL_Q);

// 幅度-频率补偿增益：1 / resonanceResponse，谐振处为 1，偏离谐振 > 1。
double frequencyCompensation(int he, double q = MECHANICAL_Q,
                             double maxBoost = MAX_FREQ_COMPENSATION);

// 对归一化目标幅度做频率补偿，返回补偿后的归一化幅度。
double compensateNormalized(double a, int he, double q = MECHANICAL_Q,
                            double maxBoost = MAX_FREQ_COMPENSATION);

}  // namespace rt
}  // namespace pcm2he
