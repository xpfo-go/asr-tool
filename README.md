# asr-tool

简体中文 | [English](docs/README_en.md)

本地语音转文字工具，基于 whisper.cpp（C++）。
配置简单：安装 `ffmpeg` 并准备模型即可运行。大模型友好：默认 `ggml-large-v3-turbo-q8_0.bin`，同时支持自动发现同目录可用的 `ggml-large-v3*.bin`。加速策略：macOS 优先 CoreML（失败回退 CPU），Linux/Windows 优先 CUDA（失败回退 CPU）。

---

## 依赖

- 必需：`ffmpeg`、模型文件（缓存目录至少一个 `ggml-large-v3*.bin`）
- 可选：`curl`（自动下载模型/CoreML）、`unzip`（macOS 解压 CoreML 包）
- `ffmpeg` 安装：macOS `brew install ffmpeg`，Linux `sudo apt install ffmpeg`，Windows `choco install ffmpeg`

---

## 1. Skill 安装

### 安装 Skill（Claude Code / Codex）

```bash
# Claude Code
git clone https://github.com/xpfo-go/asr-tool.git ~/.claude/skills/asr-tool

# Codex
git clone https://github.com/xpfo-go/asr-tool.git ~/.codex/skills/asr-tool
```

部分 Codex 环境使用 `~/.agents/skills`，请按实际路径放置。

下载对应平台二进制到 Skill 目录（按你的系统选择一条）：

```bash
SKILL_DIR=~/.claude/skills/asr-tool
ASR_VERSION=v1.0.0
mkdir -p "$SKILL_DIR/bin"

# macOS arm64
curl -fL -o "$SKILL_DIR/bin/asr" "https://github.com/xpfo-go/asr-tool/releases/download/${ASR_VERSION}/asr-macos-arm64"
chmod +x "$SKILL_DIR/bin/asr"

# Linux x86_64
curl -fL -o "$SKILL_DIR/bin/asr" "https://github.com/xpfo-go/asr-tool/releases/download/${ASR_VERSION}/asr-linux-x86_64"
chmod +x "$SKILL_DIR/bin/asr"
```

Windows x86_64（PowerShell）：
```powershell
$SKILL_DIR="$env:USERPROFILE\.claude\skills\asr-tool"
$ASR_VERSION="v1.0.0"
New-Item -ItemType Directory -Force -Path "$SKILL_DIR\bin" | Out-Null
curl.exe -fL -o "$SKILL_DIR\bin\asr.exe" "https://github.com/xpfo-go/asr-tool/releases/download/$ASR_VERSION/asr-windows-x86_64.exe"
```

---

## 2. 模型（推荐手动下载到 `.cache`）

- 默认模型：`ggml-large-v3-turbo-q8_0.bin`（约 800MB）
- 默认目录：macOS/Linux `~/.cache/whisper/`，Windows `%USERPROFILE%\\.cache\\whisper\\`
- 发现策略：优先 `ggml-large-v3-turbo-q8_0.bin`，否则自动查找同目录 `ggml-large-v3*.bin`
- 若模型文件不存在，程序会尝试自动下载（失败会提示手动下载命令）

手动下载（macOS/Linux）：
```bash
mkdir -p ~/.cache/whisper
curl -fL -o ~/.cache/whisper/ggml-large-v3-turbo-q8_0.bin "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"
```

macOS 可选：手动下载 CoreML 编码器（`mlmodelc`）
```bash
mkdir -p ~/.cache/whisper
curl -fL -o ~/.cache/whisper/ggml-large-v3-turbo-encoder.mlmodelc.zip "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-encoder.mlmodelc.zip"
unzip -o ~/.cache/whisper/ggml-large-v3-turbo-encoder.mlmodelc.zip -d ~/.cache/whisper/
```

手动下载（Windows PowerShell）：
```powershell
$dir = "$env:USERPROFILE\.cache\whisper"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
curl.exe -fL -o "$dir\ggml-large-v3-turbo-q8_0.bin" "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"
```

---

## 使用

### 1) 在 Claude Code / Codex 中使用

可直接给一句提示词，例如：

```text
请把 /path/video_path.mp4 提取文字。
```

### 2) 使用 asr 二进制

```bash
# 最简用法
asr recording.mp3

# 指定输出文件
asr meeting.m4a -o transcript.txt

# 指定语言 + prompt
asr audio.mp3 -l zh -p "药物名称、检查项目"

# 指定模型目录（目录下放 ggml-large-v3*.bin）
asr audio.mp3 -m /data/models/whisper

# 字幕格式
asr video.mov -f srt -o subtitles.srt
```

默认 `text`（`.txt`）输出按转写片段分行。
