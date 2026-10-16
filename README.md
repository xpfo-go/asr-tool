# asr-tool

简体中文 | [English](docs/README_en.md)

当前开发版本为 **v2.0.0-rc**，属于预发布版，提供本地会议转写和实时复核。
[方向审查](docs/PRODUCT_DIRECTIONS.md)说明本轮取舍。

本地语音转文字工具，支持单设备录制的多人会议、中英文混合转写、说话人区分和保守纠错。录音、转写和声纹只在本机处理。

基于 whisper.cpp、Silero VAD 和 sherpa-onnx。默认使用 `ggml-large-v3-turbo-q8_0.bin`；macOS 使用 CoreML + Metal，缺少 CoreML 编码器时继续使用 Metal。Linux/Windows 保留 CUDA → CPU 路径，说话人分析目前使用 CPU。

---

## 依赖

- 必需：`ffmpeg`、已准备的本地模型
- 模型准备：`curl`、`tar`；macOS 可选 `unzip`（CoreML 编码器）
- `ffmpeg` 安装：macOS `brew install ffmpeg`，Linux `sudo apt install ffmpeg`，Windows `choco install ffmpeg`

---

## 1. 一键安装 / 更新

推荐直接使用 latest release 的一键安装脚本；再次执行同一条命令时，会自动更新到最新 release：

```bash
curl -fsSL https://github.com/xpfo-go/asr-tool/releases/latest/download/install.sh | bash
```

试用本次候选版（发布完成后可用，稳定版 `latest` 不受影响）：

```bash
curl -fsSL https://github.com/xpfo-go/asr-tool/releases/download/v2.0.0-rc.1/install.sh | ASR_TOOL_RELEASE_TAG=v2.0.0-rc.1 bash
```

脚本会自动完成这些事情：

- 自动探测 `~/.claude/skills`、`~/.codex/skills`、`~/.agents/skills`、`~/.gemini/skills`、`~/.openclaw/skills`、`~/.hermes/skills`
- 交互式多选安装目标；默认全选全部已检测到的 Skill 目录
- 选择界面支持 `Up/Down` 移动、`Space` 勾选/取消、`a` 全选或全不选、`Enter` 确认
- Skill 目录统一安装或更新到 GitHub `latest release`
- 全局共享二进制安装到 `~/.local/bin/asr-tool`
- 全局共享模型目录使用 `~/.cache/whisper/`
- 安装期间准备 ASR、VAD 和说话人模型；之后转写无需网络
- 自动尝试安装 `ffmpeg`；若包管理器不可用则提示手动安装
- 若 `~/.local/bin` 不在 `PATH`，自动写入 shell 配置

说明：

- macOS / Linux：直接在终端运行上面的命令
- Windows：请在 Git Bash 中运行上面的命令；安装完成后 PowerShell 和 `cmd` 里也可以直接执行 `asr-tool`
- 若某个已存在的 Skill 目录有未提交改动，安装脚本会直接报错，不会覆盖你的本地修改

---

## 2. 模型

安装脚本会准备模型。源码构建后或模型缺失时，先执行一次：

```bash
asr-tool --prepare-models
```

只有模型准备命令联网下载文件。正常转写不自动下载，不调用云端服务；离线机器可以从另一台机器复制整个模型目录。

- 默认模型：`ggml-large-v3-turbo-q8_0.bin`（约 800MB）
- 实时模型：`streaming-zipformer-bilingual-zh-en-2023-02-20`（约 200MB），由 `--prepare-models` 一并准备
- 默认目录：macOS/Linux `~/.cache/whisper/`，Windows `%USERPROFILE%\\.cache\\whisper\\`
- 发现策略：优先 `ggml-large-v3-turbo-q8_0.bin`，否则自动查找同目录 `ggml-large-v3*.bin`
- VAD：`ggml-silero-v6.2.0.bin`；分段：`segmentation.onnx`；默认声纹：`3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx`。旧版 `wespeaker_en_voxceleb_resnet34_LM.onnx` 仍可兼容使用。
- 自定义目录：准备和转写均传入 `-m /path/to/models`

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

本机实时转写使用默认麦克风，按 `Ctrl-C` 结束；输出为 JSONL，默认写到标准输出：

```bash
asr-tool --live -p "项目名、型号、专业术语" -o meeting-live.jsonl
```

实时链路先用轻量本地模型出 provisional 文本，再用本地 Turbo 模型复核并输出
revision/final 事件。没有云端服务；实时模型和复核模型都必须提前通过
`asr-tool --prepare-models` 准备。公开混合语音评测的文字错误率、延迟和资源占用
见 [EVALUATION.md](docs/EVALUATION.md)。

默认结果保存在输入文件旁，扩展名为 `.txt`。会议文本带时间位置、说话人及
`[需复核]` 标记。使用 `-o meeting.json`、`-o meeting.srt` 或 `-o meeting.vtt`
即可选择格式；显式 `-f` 优先。输出路径不能与原录音相同。

## 多人会议

```bash
asr-tool meeting.mp4 -p "MAP、rolling hours、cycle、graph" -o meeting.json
```

- 自动分析整场录音，按首次发言顺序输出 `speaker_1`、`speaker_2` 等；同一文件内统一编号，不识别真实姓名。
- `-p` 直接提供用户的行业术语，无需另建词表。建议短且贴近录音内容；最多使用末尾 160 tokens。
- 用语音活动检测跳过静音，优先在停顿处分块，并延续少量已接受文本作为上下文；低置信度或明显重复的窗口会本地重解码。自动语言检测仅在当前证据较弱、仍支持前文语言时沿用前文，明确的语言切换保留原语言。
- 纠错候选改动数字或否定词、大幅改写或增加重复时不自动采用。JSON 的 `corrections` 保留原文、采用的候选和原因；`confidence` 是模型 token 分数，不是正确率。
- 会议声纹默认使用本地 ERes2Net；已安装的旧 WeSpeaker 缓存仍可离线使用，运行 `asr-tool --prepare-models` 可更新。用户无需填写人数或聚类阈值。
- 默认按声学时间对齐文字与说话人；对齐失效时整句保留，只有全句证据一致时才标注说话人，换人或证据不足则留空。`--no-speakers` 跳过说话人分析及额外对齐。
- 检测到的重叠发言标为 `overlap`；`review_reasons` 区分文字低分、重复、纠错被拒、时间近似和说话人不确定。JSON 的 `speaker_turns` 保留独立声音时间轴，`speaker_model` 记录实际模型。
- 证据不足的说话人为 `null`；短促插话、远场噪声和同声发言仍需人工复核，不做声源分离。公开会议评测与复现命令见 [EVALUATION.md](docs/EVALUATION.md)。
- 默认分析说话人。只需普通转写时用 `--no-speakers`；说话人模型缺失会告警并继续输出文本，VAD/ASR 模型缺失则报错。

全录音 PCM 用于全局说话人聚类，内存和临时 WAV 各约每小时 220MiB，另外需要模型及推理内存；长会议建议 8GB 以上内存。

## 源码构建与测试

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
build/asr-tool --prepare-models
```

构建时下载固定版本依赖，模型不进入 Git。第三方许可见 [THIRD_PARTY.md](docs/THIRD_PARTY.md)，本地会议测试结果与限制见 [VALIDATION.md](docs/VALIDATION.md)。
