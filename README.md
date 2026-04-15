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

推荐直接使用 latest release 的一键安装脚本：

```bash
curl -fsSL https://github.com/xpfo-go/asr-tool/releases/latest/download/install.sh | bash
```

脚本会自动完成这些事情：

- 自动探测 `~/.claude/skills`、`~/.codex/skills`、`~/.agents/skills`、`~/.gemini/skills`、`~/.openclaw/skills`、`~/.hermes/skills`
- 交互式多选安装目标；默认全选全部已检测到的 Skill 目录
- Skill 目录统一安装或更新到 GitHub `latest release`
- 全局共享二进制安装到 `~/.local/bin/asr-tool`
- 全局共享模型目录使用 `~/.cache/whisper/`
- 自动尝试安装 `ffmpeg`；若包管理器不可用则提示手动安装
- 若 `~/.local/bin` 不在 `PATH`，自动写入 shell 配置

说明：

- macOS / Linux：直接在终端运行上面的命令
- Windows：请在 Git Bash 中运行上面的命令；安装完成后 PowerShell 和 `cmd` 里也可以直接执行 `asr-tool`
- 若某个已存在的 Skill 目录有未提交改动，安装脚本会直接报错，不会覆盖你的本地修改

---

## 2. 模型

安装脚本会先检查 `~/.cache/whisper/` 中是否已经存在 `ggml-large-v3*.bin`。如果没有，会自动下载默认模型 `ggml-large-v3-turbo-q8_0.bin`。

- 默认模型：`ggml-large-v3-turbo-q8_0.bin`（约 800MB）
- 默认目录：macOS/Linux `~/.cache/whisper/`，Windows `%USERPROFILE%\\.cache\\whisper\\`
- 发现策略：优先 `ggml-large-v3-turbo-q8_0.bin`，否则自动查找同目录 `ggml-large-v3*.bin`
- 若模型文件不存在，程序会尝试自动下载（失败会提示手动下载命令）

手动下载（macOS/Linux）：
```bash
mkdir -p ~/.cache/whisper && curl -fL -o ~/.cache/whisper/ggml-large-v3-turbo-q8_0.bin "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"
```

macOS 可选：手动下载 CoreML 编码器（`mlmodelc`，以下命令下载的是 `ggml-large-v3-turbo` 系列对应编码器）
```bash
mkdir -p ~/.cache/whisper && curl -fL -o ~/.cache/whisper/ggml-large-v3-turbo-encoder.mlmodelc.zip "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-encoder.mlmodelc.zip" && unzip -o ~/.cache/whisper/ggml-large-v3-turbo-encoder.mlmodelc.zip -d ~/.cache/whisper/
```

手动下载（Windows PowerShell）：
```powershell
$dir = "$env:USERPROFILE\.cache\whisper"; New-Item -ItemType Directory -Force -Path $dir | Out-Null; curl.exe -fL -o "$dir\ggml-large-v3-turbo-q8_0.bin" "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"
```

---

## 使用

### 1) 在 Claude Code / Codex 中使用

可直接给一句提示词，例如：

```text
请把 /path/video_path.mp4 提取文字。
```

### 2) 使用 asr-tool 二进制

```bash
asr-tool recording.mp3
```

```bash
asr-tool meeting.m4a -o transcript.txt
```

```bash
asr-tool audio.mp3 -l zh -p "药物名称、检查项目"
```

```bash
asr-tool audio.mp3 -m /data/models/whisper
```

```bash
asr-tool video.mov -f srt -o subtitles.srt
```

默认 `text`（`.txt`）输出按转写片段分行。
