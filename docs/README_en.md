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

## 1. One-Line Install / Update

Recommended: use the one-line installer from the latest release. Re-running the same command updates everything to the newest release automatically:

```bash
curl -fsSL https://github.com/xpfo-go/asr-tool/releases/latest/download/install.sh | bash
```

The installer automatically:

- Detects `~/.claude/skills`, `~/.codex/skills`, `~/.agents/skills`, `~/.gemini/skills`, `~/.openclaw/skills`, and `~/.hermes/skills`
- Shows an interactive multi-select list and defaults to installing into every detected Skill root
- Installs or updates each selected Skill directory to the GitHub `latest release`
- Installs one shared binary at `~/.local/bin/asr-tool`
- Reuses one shared model directory at `~/.cache/whisper/`
- Tries to install `ffmpeg` automatically and falls back to manual instructions only if no package manager is available
- Adds `~/.local/bin` to PATH automatically when needed

Notes:

- macOS / Linux: run the command above in your terminal
- Windows: run it in Git Bash; after installation, `asr-tool` is available from Git Bash, PowerShell, and `cmd`
- If an existing Skill directory has local modifications, the installer exits with an error instead of overwriting your changes

---

## 2. Model

The installer first checks `~/.cache/whisper/` for any `ggml-large-v3*.bin` file. If none exists, it downloads the default `ggml-large-v3-turbo-q8_0.bin` model automatically.

- Default model: `ggml-large-v3-turbo-q8_0.bin` (~800MB)
- Default directory: macOS/Linux `~/.cache/whisper/`, Windows `%USERPROFILE%\\.cache\\whisper\\`
- Discovery strategy: prefer `ggml-large-v3-turbo-q8_0.bin`; if missing, auto-discover `ggml-large-v3*.bin` in the same directory
- If model files are missing, the program will try auto-download (and print manual commands on failure)

Manual download (macOS/Linux):
```bash
mkdir -p ~/.cache/whisper && curl -fL -o ~/.cache/whisper/ggml-large-v3-turbo-q8_0.bin "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"
```

Optional on macOS: manually download CoreML encoder (`mlmodelc`, the command below downloads the encoder for the `ggml-large-v3-turbo` series)
```bash
mkdir -p ~/.cache/whisper && curl -fL -o ~/.cache/whisper/ggml-large-v3-turbo-encoder.mlmodelc.zip "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-encoder.mlmodelc.zip" && unzip -o ~/.cache/whisper/ggml-large-v3-turbo-encoder.mlmodelc.zip -d ~/.cache/whisper/
```

Manual download (Windows PowerShell):
```powershell
$dir = "$env:USERPROFILE\.cache\whisper"; New-Item -ItemType Directory -Force -Path $dir | Out-Null; curl.exe -fL -o "$dir\ggml-large-v3-turbo-q8_0.bin" "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"
```

---

## Usage

### 1) In Claude Code / Codex

You can use a direct prompt, for example:

```text
Please transcribe /path/video_path.mp4 to text.
```

### 2) asr-tool Binary

```bash
asr-tool recording.mp3
```

```bash
asr-tool meeting.m4a -o transcript.txt
```

```bash
asr-tool audio.mp3 -l zh -p "drug names, test items"
```

```bash
asr-tool audio.mp3 -m /data/models/whisper
```

```bash
asr-tool video.mov -f srt -o subtitles.srt
```

Default `text` (`.txt`) output is written line-by-line by transcription segment.
