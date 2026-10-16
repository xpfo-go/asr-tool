# asr-tool

[简体中文](../README.md) | English

**v2.0.0-rc** is a prerelease with local meeting transcription and live audio review.

Local speech-to-text for meetings recorded on one device, with mixed-language transcription, speaker labels, and conservative correction. Audio, transcripts, and voice embeddings stay on your machine.

Built on whisper.cpp, Silero VAD, and sherpa-onnx. The default ASR model is `ggml-large-v3-turbo-q8_0.bin`. macOS uses CoreML + Metal, retaining Metal when the CoreML encoder is absent. Linux/Windows retain CUDA with CPU fallback; diarization currently uses CPU.

---

## Dependencies

- Required: `ffmpeg` and prepared local models
- Model preparation: `curl` and `tar`; optional `unzip` for the macOS CoreML encoder
- Install `ffmpeg`: macOS `brew install ffmpeg`, Linux `sudo apt install ffmpeg`, Windows `choco install ffmpeg`

---

## 1. One-Line Install / Update

Recommended: use the one-line installer from the latest release. Re-running the same command updates everything to the newest release automatically:

```bash
curl -fsSL https://github.com/xpfo-go/asr-tool/releases/latest/download/install.sh | bash
```

To try this candidate after publication, without changing the stable `latest`:

```bash
curl -fsSL https://github.com/xpfo-go/asr-tool/releases/download/v2.0.0-rc/install.sh | ASR_TOOL_RELEASE_TAG=v2.0.0-rc bash
```

The installer automatically:

- Detects `~/.claude/skills`, `~/.codex/skills`, `~/.agents/skills`, `~/.gemini/skills`, `~/.openclaw/skills`, and `~/.hermes/skills`
- Shows an interactive multi-select list and defaults to installing into every detected Skill root
- The selector supports `Up/Down` to move, `Space` to toggle, `a` to toggle all, and `Enter` to confirm
- Installs or updates each selected Skill directory to the GitHub `latest release`
- Installs one shared binary at `~/.local/bin/asr-tool`
- Reuses one shared model directory at `~/.cache/whisper/`
- Prepares ASR, VAD, and speaker models during installation; inference works offline
- Tries to install `ffmpeg` automatically and falls back to manual instructions only if no package manager is available
- Adds `~/.local/bin` to PATH automatically when needed

Notes:

- macOS / Linux: run the command above in your terminal
- Windows: run it in Git Bash; after installation, `asr-tool` is available from Git Bash, PowerShell, and `cmd`
- If an existing Skill directory has local modifications, the installer exits with an error instead of overwriting your changes

---

## 2. Model

The installer prepares the models. After building from source, or when models are missing, run:

```bash
asr-tool --prepare-models
```

Only model preparation downloads files. Transcription never downloads models or calls a cloud service. Copy the entire model directory to provision an offline machine.

- Default model: `ggml-large-v3-turbo-q8_0.bin` (~800MB)
- Live model: `streaming-zipformer-bilingual-zh-en-2023-02-20` (about 200MB), prepared by `--prepare-models`
- Default directory: macOS/Linux `~/.cache/whisper/`, Windows `%USERPROFILE%\\.cache\\whisper\\`
- Discovery strategy: prefer `ggml-large-v3-turbo-q8_0.bin`; if missing, auto-discover `ggml-large-v3*.bin` in the same directory
- Additional models: `ggml-silero-v6.2.0.bin`, `segmentation.onnx`, and the default `3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx`. Existing `wespeaker_en_voxceleb_resnet34_LM.onnx` files remain compatible.
- Pass `-m /path/to/models` for both preparation and transcription to use a custom directory

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

For local live transcription from the default macOS microphone:

```bash
asr-tool --live -p "project names, model names, domain terms" -o meeting-live.jsonl
```

The live command emits JSONL provisional, revision, and final events. It uses a
local streaming recognizer for early text and locally re-decodes completed
utterances with the Turbo model. Both models are prepared by the explicit
`asr-tool --prepare-models` command; inference never calls a cloud service.

Default `.txt` output is saved beside the input. Meeting text includes time
positions, anonymous speaker labels, and review markers. Use `-o meeting.json`,
`-o meeting.srt`, or `-o meeting.vtt` to select the format; explicit `-f` takes
precedence. Output cannot overwrite the input recording.

## Meetings

```bash
asr-tool meeting.mp4 -p "MAP, rolling hours, cycle, graph" -f json -o meeting.json
```

Speaker labels are assigned across the entire recording in first-occurrence order (`speaker_1`, `speaker_2`, ...). They are anonymous and apply to this file only. Use the existing `-p` prompt for industry terminology; keep it short and relevant. The final 160 tokens are retained.

VAD skips silence and prefers pauses as window boundaries. Windows do not overlap; a small amount of accepted text carries context forward. Automatic language selection uses continuity only when current audio weakly favors a switch and still supports the previous language. Clear switches retain the original language. Low-confidence or repetitive windows are decoded again locally. Candidates that change numbers or negations, substantially rewrite the text, or increase repetition are rejected. JSON includes `corrections` with the original and accepted replacement text; `confidence` is a token score, not an accuracy estimate.

Local ERes2Net is the default speaker model. Existing WeSpeaker installations continue to work offline; `asr-tool --prepare-models` updates the cache. No speaker count, threshold, or configuration file is required. Acoustic alignment places text on the speaker timeline. When alignment fails, the complete utterance is kept together and receives a speaker only if the evidence is consistent throughout; otherwise its speaker is unknown. `--no-speakers` skips diarization and the additional alignment. JSON adds `speaker_model`, the independent `speaker_turns` timeline, and per-segment `review_reasons`. Public meeting evaluation and limitations are documented in [EVALUATION.md](EVALUATION.md).

Overlapping speech has `overlap: true`; uncertain text has `needs_review: true`. Uncertain speakers are `null`. Brief interjections, distant microphones, and simultaneous speech still need review; the tool does not separate voices. Use `--no-speakers` to skip diarization. Missing speaker models produce a warning and text-only output; missing ASR/VAD models produce an error.

Whole-recording PCM and the temporary WAV each need about 220MiB per hour, in addition to inference memory. At least 8GB RAM is recommended for long meetings.

## Build and Test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
build/asr-tool --prepare-models
```

Build dependencies are pinned. Models are excluded from Git. See [third-party licenses](THIRD_PARTY.md) and [local meeting validation and limitations](VALIDATION.md).
