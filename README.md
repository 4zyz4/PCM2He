# PCM2He

把 **PCM 音频**离线转换为 **RichTap HE 震动效果文件**（HE 1.0 / HE 2.0 均可）。
每个输出文件是一条独立的 HE 效果，配套 `manifest.json` 描述分块信息。

算法与设备标定移植自 [GKME](https://github.com/4zyz4/GKME) 的 HD 震动实现
（`android/src/main/java/com/zyz4/gkme/haptic/`），并参考其
`docs/richtap-hd-vibration.md` 与 `docs/richtap/scripts/pcm_to_he.py`。

## 原理

把 PCM 的**幅度包络**与**带内主频**映射成 HE 的 `Intensity` / `Frequency`：

1. 读入音频，混为单声道并重采样到分析率（默认 48kHz）；
2. 40–400Hz 带通，按 40ms/5ms 分帧 RMS 的 95 百分位作为参考电平；
3. 按事件时长（默认 100ms）逐段提取 `(包络, 主频)`；
4. 主频经 `shiftIntoRange`（过低按八度上移、过高钳到上限）后查真机标定表转成 HE；
5. 每段写成一条 `continuous` 事件，曲线固定 **4 个控制点**且**聚到事件两端**
   （`0, ε, D-ε, D`），相邻事件共享边界电平，保证多文件连续播放时无断点；
6. 额外检测两类**脉冲**并作为满强度短事件并入所属分块（可与常规事件重叠）：
   - **音量突增**（`--accent-ms`）：幅度在短时间内跳升时插入一条短脉冲；
   - **短促瞬态**（`--burst-ms`）：识别“静音→短促有声→静音”的 ≤60ms 事件；
7. 按每条效果的常规事件数（默认 8）切分成多个 HE 文件，脉冲占用剩余事件槽位。

真机硬约束（见 GKME 文档）：单条效果 ≤16 个事件、每事件 `Curve` 恰好 4 点、单事件 ≤5000ms。

## 构建

需要 g++（C++17）。Windows 下：

```powershell
.\build.ps1            # 生成 build\pcm2he.exe
.\build.ps1 -Clean     # 清理后重建
```

或使用 CMake：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## 用法

```
pcm2he [选项] <输入音频>
```

```powershell
# WAV -> HE 1.0
.\build\pcm2he.exe -o out sample.wav

# WAV -> HE 2.0，每条效果 4 个事件
.\build\pcm2he.exe --version 2 --events-per-chunk 4 -o out sample.wav

# 裸 PCM（float32 单声道 48k）
.\build\pcm2he.exe --raw --format f32 --rate 48000 --channels 1 -o out src.f32

# 使用引擎次方律逆变换 + 频率补偿（更贴近机械位移线性）
.\build\pcm2he.exe --inverse-law -o out sample.wav
```

主要选项：

| 选项 | 说明 | 默认 |
|------|------|------|
| `-o, --outdir DIR` | 输出目录 | `./out` |
| `--version 1\|2` | 输出 HE 版本 | `1` |
| `--event-ms MS` | 每个事件时长（时间分辨率） | `100` |
| `--events-per-chunk N` | 每条效果/每个文件的**常规**事件数（1..16） | `8` |
| `--analysis-rate HZ` | 分析/重采样目标率 | `48000` |
| `--low HZ` / `--high HZ` | 分析带通范围 | `40` / `400` |
| `--gamma X` | 包络压缩指数 | `0.6` |
| `--inverse-law` | 频率补偿 + 引擎次方律逆变换 | 关 |
| `--silence X` | 低于 `X×参考电平` 的包络置零 | `0`（关） |
| `--accent-ms MS` | 音量突增脉冲时长 | `5`（`0`=关） |
| `--burst-ms MS` | 短促瞬态脉冲时长 | `8`（`0`=关） |
| `--pulse-hop-ms MS` | 脉冲检测帧间隔 | `10` |
| `--raw` / `--format` / `--rate` / `--channels` | 裸 PCM 输入参数 | `f32` / `48000` / `1` |

### 脉冲与事件槽位

单条 HE 效果**最多 16 个事件**（真机硬约束）。常规事件占用 `--events-per-chunk` 个槽位，
其余留给脉冲。因此：

- 脉冲密集的素材若出现 `丢弃`，请**减小** `--events-per-chunk`（如 `2`~`4`）为脉冲腾出空间；
- 若不需要脉冲，用 `--accent-ms 0 --burst-ms 0` 关闭，并可将 `--events-per-chunk` 调回 `16`；
- `--pulse-hop-ms` 应接近实际音频回调周期（默认 10ms），仅在脉冲误检/漏检时微调。

## 输出

```
out/
  chunk_000.json   # 一条 HE 效果
  chunk_001.json
  ...
  manifest.json    # 分块元信息
```

- `--version 1`：顶层为 `Pattern`；
- `--version 2`：顶层为 `PatternList`，每个事件一项，`AbsoluteTime` 为事件起始时间；
- `manifest.json`：`event_ms`、`events_per_chunk`、`chunk_ms`、`he_version`、`sample_rate`、`chunks`。

## 目录结构

```
src/
  main.cpp        CLI 入口
  audio.h/.cpp    WAV / 裸 PCM 读取、混音、重采样
  dsp.h/.cpp      双二次带通、FFT、包络/主频特征
  richtap.h/.cpp  HE<->Hz 标定表、幅度律、频率补偿
  encoder.h/.cpp  特征提取、HE 1.0/2.0 组装与分块输出
```

## 已知限制

- 脉冲检测的阈值（增益/时长/间隔）沿用 GKME 真机标定值，帧率由 `--pulse-hop-ms` 近似；
  与真机逐帧流式实现可能略有差异。
- 脉冲插入受 16 事件上限约束，超出部分会被丢弃并在运行时提示。
- 带通使用级联 RBJ 双二次而非 scipy Butterworth，包络形状略有差异；
  经参考电平归一化后对结果影响有限。
- 频率补偿的机械 Q 取固定值 `10`，未做真机标定。

## 开源协议

本项目基于 [GNU General Public License v3.0](LICENSE) 发布。
算法与标定数据移植自 [GKME](https://github.com/4zyz4/GKME)（同为 GPLv3）。
