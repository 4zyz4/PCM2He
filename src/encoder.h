#pragma once

#include <string>

#include "audio.h"

namespace pcm2he {

struct EncodeOptions {
    // 输出 HE 版本：1 = 顶层 Pattern；2 = 顶层 PatternList。
    int heVersion = 1;

    // 每个 continuous 事件的时长（ms），也是包络的时间分辨率。
    int eventMs = 100;

    // 每条效果（每个输出文件）包含的常规事件数（引擎总上限 16）。
    // 默认留出槽位给脉冲事件；如需更多平滑度可增大，但脉冲空间减少。
    int eventsPerChunk = 8;

    // 分析用带通范围（Hz）。
    double lowHz = 40.0;
    double highHz = 400.0;

    // 包络压缩指数：intensity = (rms/ref)^gamma。
    double gamma = 0.6;

    // false = 直接写归一化包络（对齐 pcm_to_he.py）；
    // true  = 先做频率补偿再取引擎次方律的逆（对齐 GKME PcmHeEncoder）。
    bool inverseLaw = false;

    // 分析采样率（输入会重采样到该值）。
    int analysisRate = 48000;

    // 低于 silenceRatio * ref 的包络强制置零（0 = 关闭）。
    double silenceRatio = 0.0;

    // 音量突增强调：检测到幅度突然增大时，额外插入一条 accentMs 时长的满强度短事件。
    // 0 = 关闭。
    int accentMs = 5;

    // 短促瞬态响应：识别“静音 -> 短促有声 -> 静音”的瞬态，额外插入一条 burstMs 时长的
    // 满强度短事件。0 = 关闭。
    int burstMs = 8;

    // 脉冲检测的帧间隔（ms），应接近实际音频回调周期。
    int pulseHopMs = 10;
};

struct EncodeStats {
    int chunks = 0;
    int events = 0;
    int sampleRate = 0;
    double durationMs = 0.0;
    double refRms = 0.0;
    int pulses = 0;         // 成功插入的脉冲事件数
    int pulsesDropped = 0;  // 因事件数达上限而丢弃的脉冲数
};

// 读取后的音频 -> 多个 HE 文件写入 outDir，并生成 manifest.json。
bool encodeToFiles(const AudioBuffer& input, const std::string& outDir,
                   const EncodeOptions& opt, EncodeStats& stats, std::string& err);

}  // namespace pcm2he
