---
name: asr-tool
description: 使用本地 asr-tool 转写音视频、区分会议说话人，或进行本机实时听写。适用于会议录音、采访、播客、字幕及离线语音转文字。
metadata:
  origin: user
---

# asr-tool

用本地 `asr-tool` 转写，不上传录音。默认输出文本；会议需要检查说话人、时间戳或纠错记录时用 `json`，字幕用 `srt`/`vtt`。

## 使用流程

1. 文件转写使用用户提供的本地音频/视频路径；用户明确要求麦克风实时听写时使用 `--live`。
2. 默认结果位于输入文件旁：`<输入文件名>.txt`。指定 `-o` 为 `.json`、`.srt` 或 `.vtt` 时自动选择格式；显式 `-f` 优先。
3. 若用户指定语言、术语、领域词或字幕格式，映射到对应参数。
   术语直接传给 `-p`，保持简短；中英文混合录音保留默认 `auto`，不翻译。
4. 执行 `asr-tool`，程序在本机自动选定结果；完成后直接交付输出文件，不增加人工校正或选择候选的使用步骤。
5. 除非用户明确要求，不要把长转写全文直接贴进回复。

## 命令

```bash
asr-tool <输入文件> [-o <输出文件>] [-l <语言>] [-p <prompt>] [-m <模型目录>] [-f <格式>] [-v]
asr-tool --live [-o <JSONL>] [-p <prompt>] [-m <模型目录>]
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
| `--no-speakers` | 跳过说话人分析，仅转写 |
| `--prepare-models` | 显式下载本地 ASR、VAD、说话人和实时流式模型；正常转写不联网 |
| `--live` | 使用 macOS 默认麦克风本地实时转写，输出 JSONL provisional/revision/final 事件 |

## 示例

```bash
asr-tool recording.mp3
asr-tool meeting.m4a -o transcript.txt
asr-tool meeting.mp4 -p "MAP、rolling hours、cycle" -o meeting.json
asr-tool doctor_recording.mp3 -l zh -p "药物名称、检查项目"
asr-tool video.mov -f srt -o subtitles.srt
```

## 输出选择

- 默认用 `text`，按连续发言整理段落，带时间位置和说话人；需要诊断信息时用 `json`。
- 文本不显示内部 `[需复核]` 标记；不确定的说话人与重叠发言仍如实标注，诊断分数和原因保留在 JSON。
- 用户要字幕：用 `srt` 或 `vtt`
- 用户要程序处理、时间戳或结构化数据：用 `json`
- 说话人编号只在当前文件内有效，不代表真实姓名，也可能拆分或合并真人。JSON 的 `speaker: null` 表示不确定，`overlap` 表示重叠发言；`needs_review` 的原因见 `review_reasons`。时间对齐失效且全句说话人不一致时保留整句未知，不按插值分配观点。`timing_approximate` 不等于文字错误，token 分数不等于正确率。
- 自动纠错仅通过本地音频重解码；`corrections` 保留变更。不要另用文本模型改写转写内容冒充识别结果。
- `--live` 只使用本机麦克风和本地流式/复核模型；按 `Ctrl-C` 结束。实时模型由 `--prepare-models` 一并准备，用户无需编辑配置文件。
- 说话人标签和通顺文字不代表经过人工核实的事实；引用观点时保留不确定标记。

## 故障处理

- `asr-tool` 不存在：检查 `~/.local/bin` 是否在 `PATH`；若当前 Skill 目录有 `install.sh`，可运行安装脚本。
- `FFmpeg 未安装`：提示用户安装 `ffmpeg`。
- 模型缺失：运行 `asr-tool --prepare-models`（需联网），或复制已准备好的模型目录并传入 `-m`。不要在离线转写中自动运行下载命令。
- 缺少说话人模型时仍可转写，但需向用户说明结果没有说话人标签。
- 文件转写保留原始和音量增强后的两路 PCM，内存约每小时 440MiB；临时 WAV 约每小时 220MiB，此外还需要推理内存。磁盘空间不足时检查临时目录。

## 退出码

- `0`：成功
- `1`：参数错误或文件不存在
- `2`：FFmpeg 缺失
- `3`：模型加载失败
- `4`：转写或输出写入错误
