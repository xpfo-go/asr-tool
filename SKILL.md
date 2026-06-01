---
name: asr-tool
description: 使用本地 asr-tool 命令将音频或视频文件转写为文本、JSON、SRT 或 VTT。适用于会议录音、播客、采访、字幕生成，以及用户要求本地/离线语音转文字的场景。
metadata:
  origin: user
---

# asr-tool

本 Skill 用本地 `asr-tool` 二进制转写音频/视频文件。默认输出普通文本；用户要求字幕时用 `srt` 或 `vtt`；用户要求结构化结果时用 `json`。

## 使用流程

1. 确认用户提供了本地音频/视频路径。
2. 若用户未指定输出路径，使用工具默认输出：`<输入文件名>.txt`。
3. 若用户指定语言、术语、领域词或字幕格式，映射到对应参数。
4. 执行 `asr-tool`，完成后告诉用户输出文件路径。
5. 除非用户明确要求，不要把长转写全文直接贴进回复。

## 命令

```bash
asr-tool <输入文件> [-o <输出文件>] [-l <语言>] [-p <prompt>] [-m <模型目录>] [-f <格式>] [-v]
```

## 参数

| 参数 | 用途 |
|------|------|
| `-o, --output <路径>` | 指定输出文件 |
| `-l, --language <语言>` | 指定语言，如 `zh`、`en`、`ja`、`auto` |
| `-p, --prompt <文本>` | 提供术语、姓名、领域词等识别提示 |
| `-m, --model-dir <目录>` | 指定包含 `ggml-large-v3*.bin` 的模型目录 |
| `-f, --format <格式>` | `text`、`json`、`srt`、`vtt` |
| `-v, --verbose` | 输出详细日志 |

## 示例

```bash
asr-tool recording.mp3
asr-tool meeting.m4a -o transcript.txt
asr-tool doctor_recording.mp3 -l zh -p "药物名称、检查项目"
asr-tool video.mov -f srt -o subtitles.srt
```

## 输出选择

- 默认用 `text`
- 用户要字幕：用 `srt` 或 `vtt`
- 用户要程序处理、时间戳或结构化数据：用 `json`

## 故障处理

- `asr-tool` 不存在：检查 `~/.local/bin` 是否在 `PATH`；若当前 Skill 目录有 `install.sh`，可运行安装脚本。
- `FFmpeg 未安装`：提示用户安装 `ffmpeg`。
- 模型缺失：将 `ggml-large-v3*.bin` 放入模型目录，或等待工具自动下载默认模型。
- 长音频会先转换为临时 WAV，再按 30 秒分块转写；磁盘空间不足时需清理临时目录或更换临时目录。

## 退出码

- `0`：成功
- `1`：参数错误或文件不存在
- `2`：FFmpeg 缺失
- `3`：模型加载失败
- `4`：转写或输出写入错误
