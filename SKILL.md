---
name: asr-tool
description: 将本地音频/视频转写为文本。适用于会议纪要、录音整理、字幕生成，以及需要离线语音转文字的场景。
metadata:
  origin: user
---

# asr-tool — 本地语音转文字

## 何时激活

- 用户要求将音频/视频文件转写为文字
- 需要快速将会议记录、播客、录音等转为文本
- 不想使用云端 API（如 OpenAI Whisper API），需要本地处理
- 需要多语言转写，支持 prompt 引导提高特定领域准确率

## 安装使用

### 方式一：直接使用预编译程序（推荐）

Skill 根目录下 `bin/` 已包含预编译程序。请先设置安装目录：

```bash
# 根据你的环境设置安装目录
# Claude Code: SKILL_DIR=~/.claude/skills/asr-tool
# Codex:       SKILL_DIR=~/.codex/skills/asr-tool
SKILL_DIR=~/.claude/skills/asr-tool

# macOS Apple Silicon（CoreML 加速）
$SKILL_DIR/bin/macos-arm64/asr recording.mp3 -o output.txt

# Linux x86_64（CUDA 加速，有 NVIDIA 显卡时自动启用）
$SKILL_DIR/bin/linux-x86_64/asr recording.mp3 -o output.txt

# Windows x86_64（CUDA 加速，有 NVIDIA 显卡时自动启用）
$SKILL_DIR/bin/windows-x86_64/asr.exe recording.mp3 -o output.txt
```

预编译程序已包含对应平台的最优加速支持：

| 平台 | 加速方案 | 说明 |
|------|----------|------|
| macOS (Apple Silicon) | CoreML | 自动启用，性能接近 GPU |
| macOS (Intel) | CPU | 自动回退 |
| Linux | CUDA → CPU | 有 NVIDIA 显卡 → CUDA，否则 → CPU |
| Windows | CUDA → CPU | 有 NVIDIA 显卡 → CUDA，否则 → CPU |

### 方式二：自行编译

如果需要自定义编译（例如交叉编译或更新 whisper.cpp 版本），在项目目录下执行：

```bash
# macOS
./scripts/build-macos.sh

# Linux
./scripts/build-linux.sh

# Windows
.\scripts\build-windows.ps1
```

编译需要：CMake、Xcode Command Line Tools (macOS)、FFmpeg。

## 前置条件

### FFmpeg（强依赖）

程序启动时检测 FFmpeg 是否可用，若缺失则报错退出：

| 平台 | 安装命令 |
|------|----------|
| macOS | `brew install ffmpeg` |
| Linux | `sudo apt install ffmpeg` |
| Windows | `choco install ffmpeg` 或 `winget install ffmpeg` |

### 模型文件

首次运行时会自动检测 `~/.cache/whisper/ggml-large-v3-turbo-q8_0.bin`（约 800MB）。

若不存在，程序会输出手动下载命令：

```bash
mkdir -p ~/.cache/whisper
curl -fSL -o ~/.cache/whisper/ggml-large-v3-turbo-q8_0.bin \
  "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"
```

## 命令行接口

```
asr <输入文件> [-o <输出文件>] [-l <语言>] [-p <prompt>] [-f <格式>] [-v]
```

### 参数说明

| 参数 | 必填 | 默认值 | 说明 |
|------|------|--------|------|
| `<输入文件>` | 是 | — | 音频/视频文件路径 |
| `-o, --output` | 否 | `<输入>.txt` | 输出路径 |
| `-l, --language` | 否 | `auto` | 语言，如 `zh`、`en`、`ja`、`auto` |
| `-p, --prompt` | 否 | — | 提示文本，引导模型输出 |
| `-f, --format` | 否 | `text` | 输出格式：`text`/`json`/`srt`/`vtt` |
| `-v, --verbose` | 否 | false | 显示详细日志 |
| `-h, --help` | 否 | — | 显示帮助 |
| `--version` | 否 | — | 显示版本 |

### 使用示例

```bash
# 最简用法 — 自动检测语言，输出到 input.mp3.txt
asr recording.mp3

# 指定输出文件
asr meeting.m4a -o transcript.txt

# 指定语言 + prompt（提高医疗术语准确率）
asr doctor_recording.mp3 -l zh -p "药物名称、检查项目"

# 输出为 SRT 字幕格式
asr video.mov -f srt -o subtitles.srt

# 显示详细日志
asr audio.wav -o out.txt -v
```

### Prompt 使用技巧

- **有帮助**: `"The speaker is discussing software architecture patterns including microservices and event-driven design"`
- **有害**: 太长或与音频内容无关的 prompt
- **适用场景**: 专有名词（产品名、技术术语）、特定口音引导、多语言混合场景

## 加速策略（平台优先级）

### macOS

```
优先 → CoreML（Apple Silicon / Intel Mac 均支持）
  ↓ 失败
回退 → CPU（Accelerate 框架）
```

首次运行自动探测并输出日志：

```
[INFO] 检测加速方案: macOS — 尝试 CoreML... 成功
[INFO] 使用加速: CoreML
[INFO] 加载模型: ggml-large-v3-turbo
[INFO] 开始转写: recording.mp3 (00:05:30)
[DONE] 输出已保存: recording.mp3.txt
```

### Windows / Linux

```
优先 → CUDA（NVIDIA GPU）
  ↓ 失败（无 NVIDIA / CUDA 不可用）
回退 → CPU
```

## 输出格式

### text（默认）

纯文本，适合直接阅读：

```
这是一段会议录音的转写结果。
第二行内容在这里。
```

### json

结构化输出，便于程序处理：

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

带时间戳的字幕格式：

```
1
00:00:00,000 --> 00:00:05,200
这是一段会议录音的转写结果。
```

### vtt

WebVTT 字幕格式（支持 web 播放）：

```webvtt
WEBVTT

00:00:00.000 --> 00:00:05.200
这是一段会议录音的转写结果。
```

## 错误处理

| 场景 | 表现 | 解决方案 |
|------|------|----------|
| FFmpeg 未安装 | `[ERROR] FFmpeg 未安装` + 平台安装提示 | 安装对应平台 FFmpeg |
| 模型文件不存在 | `[ERROR] 模型文件不存在` + 下载命令 | 下载模型到 `~/.cache/whisper/` |
| 文件不存在 | `[ERROR] 文件不存在: xxx` | 检查文件路径 |
| 文件格式不支持 | `[ERROR] 不支持的格式，请使用音频/视频文件` | 确保输入是有效的音视频文件 |
| 加速失败 | 自动回退到 CPU，继续运行 | 无需干预 |

### 退出码

```
0  — 成功
1  — 参数错误 / 文件不存在
2  — FFmpeg 缺失
3  — 模型加载失败
4  — 转写过程错误
```

## 注意事项

- **FFmpeg 是强依赖**：未安装时程序会直接退出，请先安装
- **首次运行**：需要联网下载模型（约 800MB），之后可离线使用
- **长音频**：程序使用流式分块处理，音频长度无限制，内存占用恒定
- **macOS CoreML**：Apple Silicon 推荐使用，性能接近 GPU
- **语言检测**：`auto` 模式首次转写会额外消耗约 1-2 秒用于语言检测
