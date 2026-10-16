# asr-tool 需求与实现约束

本工具处理单设备录制的多人会议，面向本地命令行及 Claude Code / Codex Skill。
优先验证 Apple Silicon；Linux/Windows 保留现有 CUDA/CPU 构建路径。

## 使用与隐私

- 最简调用为 `asr-tool meeting.mp4`，默认输出带匿名说话人标签的文本。
- 不上传音频、文本或声纹，不调用云端识别或文本改写服务。
- 模型仅在安装或显式 `--prepare-models` 时下载。正常推理缺少 ASR/VAD 模型就报错；
  缺少说话人模型则告警并继续转写。
- 用户术语沿用 `-p/--prompt`，不引入配置文件或独立词表。提示最多使用末尾 160 tokens。
- macOS 实时模式使用 `asr-tool --live` 和默认麦克风，输出 JSONL；用户只需可选 `-p`，按 `Ctrl-C` 结束。
- 默认逐窗口检测语言；弱语言证据可沿用仍受当前音频支持的前文语言，不翻译、不锁定整场。
- 输出支持 `text/json/srt/vtt`，保留 `-o/-m/-v`；`--no-speakers` 可跳过说话人分析。
- 默认输出在原文件旁；`-o` 扩展名自动选择格式，`-f` 优先。路径错误和覆盖原音频提前拒绝。
- 普通会议文本显示时间位置与复核标记，精确原因与原文变更仍由 JSON 保存。

完整参数与安装方式见 [README.md](../README.md)。

## 本地处理链路

1. FFmpeg 仅允许本地文件/管道协议，转换为 16kHz 单声道 float32 WAV，读取全录音 PCM。
2. Silero VAD 检测语音，优先在停顿处划分不超过 26 秒的窗口。窗口不重叠，
   避免不可靠的词级时间戳在边界造成漏字。保留最多 64 tokens 的已接受文本作为上下文，
   长停顿或低置信度窗口后清空。
3. sherpa-onnx 对整场录音分段、提取 ERes2Net 声纹并聚类，自动估计人数；编号按首次出现排列。
   保留旧 WeSpeaker 缓存的离线兼容，阈值随模型在内部选择。
   不识别真实姓名，不跨文件保存声纹，不做声源分离。
4. whisper.cpp 使用本地 large-v3 模型转写，默认 large-v3-turbo q8。低分或明显重复的
   窗口用 beam search 重解码；候选需提高 token 分数、保留数字和否定词、不增加重复，
   忽略大小写和常见标点后的字符编辑比例不超过 25%。
   这些规则只能保守筛选，不能证明语义正确。
5. 默认按声学时间对齐文字和说话人；对齐失效时保留整句，标记近似时间。
   只有全句说话人证据一致时才分配标签，否则整句未知，禁止按文字插值推断换人位置。
   `--no-speakers` 跳过说话人分析和额外声学对齐。
6. 写出结果。JSON 提供 `speaker`、`overlap`、`needs_review`、`confidence`、
   `review_reasons`、`speaker_turns`、`speaker_model`、`corrections` 和 `warnings`。
   时间对齐风险与文字低分分开标记；token 分数不是识别正确率。
7. 实时模式由 AVAudioEngine 采集并重采样到单声道 16 kHz；采集队列有界，先输出
   流式 provisional，再用本地 Turbo 模型复核并输出 revision/final。采集或输出失败
   以失败状态结束；单句复核失败保留原文并在 final 中记录 `review_failed`，继续处理后续语音。
   队列满时明确记录未复核状态，不能静默丢音频。

## 资源与平台

- Apple Silicon 的 ASR 使用 CoreML + Metal；无 CoreML 编码器时保留 Metal。
  说话人分析目前使用 CPU，两个推理线程。
- Linux/Windows 的 ASR 通过动态后端加载 CUDA，不能加载时使用 CPU。
  本轮没有这些平台的运行验证，不能宣称已验证 NVIDIA 性能。
- 全录音 PCM 与临时 WAV 各约每小时 220MiB，此外还有模型、中间张量及聚类内存。
  内存随录音长度增长；临时文件在读取结束或异常时清理。
- FFmpeg 是运行依赖。模型准备需要 curl/tar，macOS 的可选 CoreML 下载还需要 unzip。
- `--live` 需要 macOS 麦克风权限；实时流式模型由 `--prepare-models` 准备，正常运行不联网。
- 模型和测试录音不提交到 Git。依赖和许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。

## 验收边界

- 禁止网络的系统环境中完成真实录音转写。
- 9 月 24 日的完整会议与短录音用于集成验证；无人工参考稿时不报告 CER/WER 提升。
- 公开会议评测固定来源、区间、校验值；未知说话人计为漏判、包含重叠区间，报告 DER/JER。
  逐字稿只来自人工参考标注；开发评测工具不成为产品运行依赖，见 [EVALUATION.md](EVALUATION.md)。
- 单元测试覆盖分段所有权、时间戳退化、UTF-8/英文词拼接、重复词保留、
  不确定/重叠说话人、数字与否定保护、四种输出和写入失败。
- 远场、短插话、同声发言及中英文术语仍可能错误，保留复核标记和纠错前后文本。

退出码：`0` 成功，`1` 参数/输入错误，`2` FFmpeg 缺失，`3` 模型加载失败，
`4` 转写或输出失败。
