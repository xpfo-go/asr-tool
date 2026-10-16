#!/usr/bin/env bash
# build-macos.sh — macOS 构建脚本（macOS-arm64/x86_64 均适用）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${PROJECT_ROOT}/build"
INSTALL_DIR="${PROJECT_ROOT}/bin/macos-$(uname -m)"

info()  { echo -e "[INFO] $*"; }
error() { echo -e "[ERROR] $*" >&2; }

# 检查依赖
if ! command -v cmake >/dev/null 2>&1; then
    error "缺少 cmake，请先安装: brew install cmake"
    exit 1
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
    error "缺少 ffmpeg，请先安装: brew install ffmpeg"
    exit 1
fi

info "开始构建 asr-tool..."
mkdir -p "${INSTALL_DIR}"

cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j"$(sysctl -n hw.ncpu)"

cmake --install "${BUILD_DIR}" --prefix "${PROJECT_ROOT}/bin/macos-$(uname -m)"
info "构建完成: ${INSTALL_DIR}/bin/asr-tool"

# 检查 CoreML 模型
MODEL_PATH="${HOME}/.cache/whisper/ggml-large-v3-turbo-q8_0.bin"
if [[ ! -s "${MODEL_PATH}" ]]; then
    info "模型文件不存在，请运行 ${INSTALL_DIR}/bin/asr-tool --prepare-models"
fi
