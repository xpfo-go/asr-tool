# asr-tool

本地语音转文字工具，基于 whisper.cpp C++ 实现。

## CI/CD

仓库已配置 GitHub Actions：
- 三平台编译校验：Linux / macOS / Windows
- 三平台二进制构建产物通过 Actions Artifacts 提供下载
- 发布时通过 Release Workflow 上传到 GitHub Releases

## 规范说明

- `SKILL.md` 采用 Agent Skills 规范 frontmatter（`name`、`description`）。
- 自定义信息放在 `metadata` 字段中，避免使用未声明的顶层字段。
- Skill 目录结构符合 `<skills>/<skill-name>/SKILL.md`。

## 安装

### 1) 安装 Skill（给 Claude Code / Codex）

Skill 的安装只依赖仓库源码，不依赖 `bin/` 目录。直接克隆到 skills 目录即可：

```bash
# Claude Code
git clone https://github.com/xpfo-go/asr-tool.git ~/.claude/skills/asr-tool

# Codex
git clone https://github.com/xpfo-go/asr-tool.git ~/.codex/skills/asr-tool
```

安装后 Claude Code / Codex 会自动识别 skill，语音转写需求时会激活。

部分 Codex 环境也可能使用 `~/.agents/skills`，若你的环境如此，请将目标路径替换为该目录。

### 2) 安装可执行文件（给最终运行）

优先从 GitHub Releases 下载对应平台二进制；平时测试也可从 Actions Artifacts 下载。

### 3) 自行编译

```bash
git clone https://github.com/xpfo-go/asr-tool.git
cd asr-tool
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cp build/asr /usr/local/bin/
```

### 依赖

- **FFmpeg**（强依赖）

```bash
# macOS
brew install ffmpeg

# Linux
sudo apt install ffmpeg

# Windows
choco install ffmpeg
```

## 使用方法

```bash
# 最简用法
asr recording.mp3

# 指定输出文件
asr meeting.m4a -o transcript.txt

# 指定语言 + prompt
asr audio.mp3 -l zh -p "药物名称、检查项目"

# 输出为 SRT 字幕
asr video.mov -f srt -o subtitles.srt
```

## 加速策略

- **macOS**: CoreML → CPU
- **Linux/Windows**: CUDA → CPU

程序会自动检测并选择最佳加速方案。

## 注意事项

- 模型文件（`ggml-large-v3-turbo-q8_0.bin`，约 800MB）首次运行时会自动下载到缓存目录（默认 `~/.cache/whisper/`）
- FFmpeg 是强依赖，未安装时程序会直接退出
- 支持任意长度的音频，流式分块处理，内存占用恒定
- GitHub 仓库单文件上限 100MB，超大二进制会通过 Artifacts / Releases 分发，而不是直接回写仓库
