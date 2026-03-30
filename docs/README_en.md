# asr-tool

[简体中文](../README.md) | English

Local speech-to-text tool built on whisper.cpp (C++).
Simple setup: install `ffmpeg` and prepare model files. Large-model friendly: defaults to `ggml-large-v3-turbo-q8_0.bin` and can auto-discover `ggml-large-v3*.bin` in the same directory. Acceleration policy: macOS prefers CoreML (falls back to CPU), Linux/Windows prefer CUDA (fall back to CPU).

---

## Dependencies

- Required: `ffmpeg`, and at least one `ggml-large-v3*.bin` model in cache/model directory
- Optional: `curl` (auto-download model/CoreML), `unzip` (extract CoreML package on macOS)
- Install `ffmpeg`: macOS `brew install ffmpeg`, Linux `sudo apt install ffmpeg`, Windows `choco install ffmpeg`

---

## 1. Skill Installation

### Install Skill (Claude Code / Codex)

```bash
# Claude Code
git clone https://github.com/xpfo-go/asr-tool.git ~/.claude/skills/asr-tool

# Codex
git clone https://github.com/xpfo-go/asr-tool.git ~/.codex/skills/asr-tool
```

Some Codex environments use `~/.agents/skills`; adjust the target path if needed.

---

## 2. Model (Recommended: manually place in `.cache`)

- Default model: `ggml-large-v3-turbo-q8_0.bin` (~800MB)
- Default directory: macOS/Linux `~/.cache/whisper/`, Windows `%USERPROFILE%\\.cache\\whisper\\`
- Discovery strategy: prefer `ggml-large-v3-turbo-q8_0.bin`; if missing, auto-discover `ggml-large-v3*.bin` in the same directory
- If model files are missing, the program will try auto-download (and print manual commands on failure)

Manual download (macOS/Linux):
```bash
mkdir -p ~/.cache/whisper
curl -fL -o ~/.cache/whisper/ggml-large-v3-turbo-q8_0.bin "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"
```

Optional on macOS: manually download CoreML encoder (`mlmodelc`)
```bash
mkdir -p ~/.cache/whisper
curl -fL -o ~/.cache/whisper/ggml-large-v3-turbo-encoder.mlmodelc.zip "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-encoder.mlmodelc.zip"
unzip -o ~/.cache/whisper/ggml-large-v3-turbo-encoder.mlmodelc.zip -d ~/.cache/whisper/
```

Manual download (Windows PowerShell):
```powershell
$dir = "$env:USERPROFILE\.cache\whisper"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
curl.exe -fL -o "$dir\ggml-large-v3-turbo-q8_0.bin" "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"
```

---

## Usage

### 1) In Claude Code / Codex

You can use a direct prompt, for example:

```text
Please transcribe /path/video_path.mp4 to text.
```

### 2) asr Binary

```bash
# Minimal
asr recording.mp3

# Output file
asr meeting.m4a -o transcript.txt

# Language + prompt
asr audio.mp3 -l zh -p "drug names, test items"

# Custom model directory (contains ggml-large-v3*.bin)
asr audio.mp3 -m /data/models/whisper

# Subtitle output
asr video.mov -f srt -o subtitles.srt
```

Default `text` (`.txt`) output is written line-by-line by transcription segment.
