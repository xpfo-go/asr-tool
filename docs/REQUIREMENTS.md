# asr-tool — 需求文档

## 1. 项目定位

为 Claude Code / Codex 设计的**零配置、命令行极简**的本地语音转文字工具。

- **语言**: C++（C++17）
- **推理引擎**: whisper.cpp（ggml-large-v3-turbo）
- **分发方式**: GitHub git clone 后直接运行预编译二进制
- **配置文件**: 无

## 2. 核心设计原则

| 原则 | 说明 |
|------|------|
| 零配置 | 所有行为通过命令行参数控制，无 YAML/JSON 配置文件 |
| 即装即用 | git clone 后有对应平台的预编译程序，下载模型后可直接运行 |
| 模型内置 | 使用 `ggml-large-v3-turbo`，首次运行自动下载到 `~/.cache/whisper/` |
| 多平台兼容 | macOS / Windows / Linux 各有预编译二进制 |
| 优雅降级 | 加速不可用时自动回退，不需用户干预 |
| 无限音频长度 | 流式分块处理，音频长度无限制，内存占用恒定 |

## 3. 命令行接口

```
asr-tool <输入文件> [-o <输出文件>] [-l <语言>] [-p <prompt>] [-f <格式>] [-v]
```

### 参数

| 参数 | 必填 | 默认值 | 说明 |
|------|------|--------|------|
| `<输入文件>` | 是 | — | 音频/视频文件路径 |
| `-o, --output` | 否 | `<输入>.txt` | 输出路径 |
| `-l, --language` | 否 | `auto` | 语言，如 `zh`/`en`/`ja`/`auto` |
| `-p, --prompt` | 否 | — | 提示文本，引导模型输出 |
| `-f, --format` | 否 | `text` | 输出格式：`text`/`json`/`srt`/`vtt` |
| `-v, --verbose` | 否 | false | 显示详细日志 |
| `-h, --help` | 否 | — | 帮助 |
| `--version` | 否 | — | 版本 |

### 示例

```bash
# 最简用法
asr-tool recording.mp3

# 指定输出文件
asr-tool meeting.m4a -o transcript.txt

# 指定语言 + prompt
asr-tool doctor_recording.mp3 -l zh -p "药物名称、检查项目"

# 输出 SRT 字幕
asr-tool video.mov -f srt -o subtitles.srt

# 显示详细日志
asr-tool audio.wav -v
```

### Prompt 使用技巧

- **有帮助**: `"The speaker is discussing software architecture patterns including microservices and event-driven design"`
- **有害**: 太长或与音频内容无关的 prompt
- **适用场景**: 专有名词（产品名、技术术语）、特定口音引导

## 4. 无限音频长度支持

whisper.cpp 本身对音频长度没有硬编码限制，但长音频一次性加载到内存会导致 OOM。

### 分块处理策略

```
音频文件 → 流式分块读取（如每 30 秒一段）
        → whisper 逐段推理
        → 结果拼接（segments 汇总 + 时间戳偏移修正）
```

### 核心设计

- **流式读取**: 音频文件通过 FFmpeg 管道处理，边读边写，避免一次性加载整个文件
- **分块大小**: 每块 30 秒音频（与 whisper 的上下文窗口匹配）
- **时间戳修正**: 每块结果的时间戳需要累加前序块的时长，拼接时做偏移修正
- **内存占用**: 恒定，不随音频时长增长（仅与模型权重 + 单块音频大小相关）
- **适用场景**: 1 小时、2 小时甚至更长的音频均可处理

### 实现要点

| 环节 | 说明 |
|------|------|
| 音频读取 | 通过 FFmpeg 管道输出 16kHz mono float PCM 流式读取 |
| 分块控制 | 按固定采样点数分块（如 480000 samples = 30s @ 16kHz） |
| 时间戳偏移 | 记录每个 chunk 的起始时间，后续 segments 的 start/end 加上累积偏移 |
| 结果拼接 | 逐块追加 segments，最终输出完整的带时间戳转写结果 |
| JSON/SRT/VTT 输出 | 时间戳连续（无重复），反映真实音频位置 |

### 内存占用估算

| 组件 | 内存占用 |
|------|----------|
| 模型权重（ggml-large-v3-turbo） | ~1.5GB |
| 单块音频缓冲（30s @ 16kHz float） | ~3.8MB |
| 模型推理中间张量 | ~500MB（视加速后端而定） |
| **总计（峰值）** | **~2-3GB**（不随音频时长增长） |

## 5. 加速策略

### macOS

```
CoreML → CPU（回退）
```

- **CoreML**: 优先尝试。Apple Silicon / Intel Mac 均支持
- **CPU**: BLAS/Accelerate 框架多线程加速
- 首次运行自动探测，日志输出当前使用的加速方案

### Windows / Linux

```
CUDA → CPU（回退）
```

- **CUDA**: 优先尝试。需 NVIDIA 驱动 + CUDA Toolkit
- **CPU**: OpenBLAS 多线程加速，无 NVIDIA 时使用

### 日志示例

```
[INFO] macOS — 尝试 CoreML... 成功
[INFO] 使用加速: CoreML
[INFO] 加载模型: ggml-large-v3-turbo
[INFO] 开始转写: recording.mp3 (00:05:30)
[DONE] 输出已保存: recording.mp3.txt
```

```
[INFO] Linux — 尝试 CUDA... 失败 (未找到 NVIDIA 驱动)
[INFO] 回退到 CPU 模式
[INFO] 加载模型: ggml-large-v3-turbo
...
```

## 6. 依赖

### 强依赖

| 依赖 | 用途 | 安装方式 |
|------|------|----------|
| **FFmpeg** | 音频格式预处理（支持 mp3/m4a/ogg/mp4 等） | macOS: `brew install ffmpeg` / Linux: `apt install ffmpeg` / Windows: `choco install ffmpeg` 或 `winget install ffmpeg` |
| whisper.cpp | ASR 推理引擎 | 编译时静态链接，无需运行时依赖 |

> **重要**: FFmpeg 是强依赖。程序启动时会检测 FFmpeg 是否可用，若缺失则直接报错退出并给出对应平台的安装提示。

### 模型

- **文件**: `ggml-large-v3-turbo-q8_0.bin`（约 800MB）
- **下载**: 首次运行自动从 HuggingFace 下载到 `~/.cache/whisper/`
- **URL**: `https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin`
- **缓存**: 下载后缓存在 `~/.cache/whisper/`，后续运行直接使用，无需重复下载

### 平台特定（编译时）

| 平台 | 加速后端 | 编译要求 |
|------|----------|----------|
| macOS (Apple Silicon) | CoreML + Metal | Xcode Command Line Tools, CMake |
| macOS (Intel) | CPU | Xcode Command Line Tools, CMake |
| Linux + NVIDIA | CUDA + cuDNN | CUDA Toolkit, CMake |
| Linux (无 NVIDIA) | CPU (OpenBLAS) | CMake |
| Windows + NVIDIA | CUDA + cuDNN | MSVC, CMake |
| Windows (无 NVIDIA) | CPU (OpenBLAS) | CMake |

## 7. 输出格式

### text（默认）

```
这是一段会议录音的转写结果。
第二行内容在这里。
```

### json

```json
{
  "text": "这是一段会议录音的转写结果。",
  "segments": [
    {
      "start": 0.0,
      "end": 5.2,
      "text": "这是一段会议录音的转写结果。"
    }
  ],
  "language": "zh",
  "model": "ggml-large-v3-turbo",
  "duration": 5.2
}
```

### srt

```
1
00:00:00,000 --> 00:00:05,200
这是一段会议录音的转写结果。

2
00:00:05,200 --> 00:00:10,500
第二行内容在这里。
```

### vtt

```webvtt
WEBVTT

00:00:00.000 --> 00:00:05.200
这是一段会议录音的转写结果。

00:00:05.200 --> 00:00:10.500
第二行内容在这里。
```

## 8. 错误处理

| 场景 | 表现 | 解决方案 |
|------|------|----------|
| FFmpeg 未安装 | `[ERROR] FFmpeg 未安装，请先安装` + 平台安装提示 | 安装对应平台 FFmpeg |
| 模型下载失败 | `[ERROR] 模型下载失败，请检查网络` + 手动下载提示 | 检查网络或手动下载到 `~/.cache/whisper/` |
| 文件不存在 | `[ERROR] 文件不存在: xxx` | 检查文件路径 |
| 文件格式不支持 | `[ERROR] 不支持的格式，请使用音频/视频文件` | 确认是音视频文件 |
| 加速失败 | 自动回退到 CPU，继续运行 | 无需干预 |

### 退出码

```
0  — 成功
1  — 参数错误 / 文件不存在
2  — FFmpeg 缺失
3  — 模型下载/加载失败
4  — 转写过程错误
```

## 9. 仓库结构

```
asr-tool/
├── SKILL.md                    # Skill 定义（给 Claude Code / Codex 用）
├── README.md                   # 用户使用说明
├── REQUIREMENTS.md             # 本文档
├── CMakeLists.txt              # CMake 构建配置
├── src/
│   ├── main.cpp                # CLI 入口，参数解析
│   ├── transcriber.cpp         # 核心转写逻辑
│   ├── platform.cpp            # 平台检测与加速选择
│   ├── output.cpp              # 格式化输出（text/json/srt/vtt）
│   └── audio.cpp               # 音频读取与 FFmpeg 封装
├── cmake/
│   └── ...
├── scripts/
│   ├── build-macos.sh          # macOS 构建脚本
│   ├── build-linux.sh          # Linux 构建脚本
│   └── build-windows.ps1       # Windows 构建脚本
└── .gitignore
```

**注意**: 模型文件不提交到仓库，首次运行时自动下载（~800MB）。

## 10. 注意事项

1. **FFmpeg 是强依赖**：SKILL.md 中必须说明未安装时的安装方式。
2. **模型首次下载**：需要联网，首次运行后即可离线使用。
3. **内存占用**：ggml-large-v3-turbo 约需 3-4GB RAM，长音频（>1小时）建议在 8GB+ 机器上运行。
4. **macOS CoreML**：Apple Silicon 推荐使用，性能接近 GPU。Intel Mac 使用 CPU。
5. **Windows/Linux CUDA**：需要 NVIDIA 显卡和 CUDA 环境，否则自动回退 CPU。
6. **跨平台二进制**：预编译二进制放在 `bin/` 目录下，按平台/架构组织。
