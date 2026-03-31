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

## 命令行接口

请使用 Skill 二进制绝对路径（不要直接调用 `asr`，避免与 macOS 系统 `/usr/sbin/asr` 冲突）：

```bash
<ASR_BIN> <输入文件> [-o <输出文件>] [-l <语言>] [-p <prompt>] [-m <模型目录>] [-f <格式>] [-v]
```

```bash
# macOS / Linux
ASR_BIN="$HOME/.claude/skills/asr-tool/bin/asr"   # 或 ~/.codex/skills/asr-tool/bin/asr 或 ~/.agents/skills/asr-tool/bin/asr
```

```powershell
# Windows（注意 .exe）
$ASR_BIN="$env:USERPROFILE\.claude\skills\asr-tool\bin\asr.exe"   # 或 $env:USERPROFILE\.codex\skills\asr-tool\bin\asr.exe 或 $env:USERPROFILE\.agents\skills\asr-tool\bin\asr.exe
```

### 参数说明

| 参数 | 必填 | 默认值 | 说明 |
|------|------|--------|------|
| `<输入文件>` | 是 | — | 音频/视频文件路径 |
| `-o, --output` | 否 | `<输入>.txt` | 输出路径 |
| `-l, --language` | 否 | `auto` | 语言，如 `zh`、`en`、`ja`、`auto` |
| `-p, --prompt` | 否 | — | 提示文本，引导模型输出 |
| `-m, --model-dir` | 否 | `~/.cache/whisper` | 模型目录（目录中放置 `ggml-large-v3*.bin`） |
| `-f, --format` | 否 | `text` | 输出格式：`text`/`json`/`srt`/`vtt` |
| `-v, --verbose` | 否 | false | 显示详细日志 |
| `-h, --help` | 否 | — | 显示帮助 |
| `--version` | 否 | — | 显示版本 |

### 常用示例

```bash
# 最简用法
<ASR_BIN> recording.mp3

# 查看版本
<ASR_BIN> --version

# 指定输出文件
<ASR_BIN> meeting.m4a -o transcript.txt

# 指定语言与 prompt
<ASR_BIN> doctor_recording.mp3 -l zh -p "药物名称、检查项目"

# 指定模型目录
<ASR_BIN> audio.mp3 -m /data/models/whisper

# 输出为 SRT 字幕格式
<ASR_BIN> video.mov -f srt -o subtitles.srt
```

## 输出格式

- `text`（默认）：按转写片段逐行输出，便于阅读
- `json`：结构化输出，便于程序处理
- `srt` / `vtt`：字幕格式输出

## 错误处理

- `FFmpeg 未安装`：安装对应平台 `ffmpeg`
- `模型文件不存在`：将 `ggml-large-v3*.bin` 放入模型目录
- `输入文件无效`：检查路径和文件格式

### 退出码

```
0  — 成功
1  — 参数错误 / 文件不存在
2  — FFmpeg 缺失
3  — 模型加载失败
4  — 转写过程错误
```
