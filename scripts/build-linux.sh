#!/usr/bin/env bash
# build-linux.sh — Linux 构建脚本（x86_64 / NVIDIA GPU 可选）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${PROJECT_ROOT}/build"
INSTALL_DIR="${PROJECT_ROOT}/bin/linux-x86_64"

info()  { echo -e "[INFO] $*"; }
error() { echo -e "[ERROR] $*" >&2; }

# 检查依赖
if ! command -v cmake >/dev/null 2>&1; then
    error "缺少 cmake，请先安装: sudo apt install cmake"
    exit 1
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
    error "缺少 ffmpeg，请先安装: sudo apt install ffmpeg"
    exit 1
fi

info "开始构建 asr-tool..."

# 检查 CUDA
if command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi -L >/dev/null 2>&1; then
    info "检测到 NVIDIA GPU，将启用 CUDA 加速"
    CUDA_FLAG="-DASR_ENABLE_CUDA=ON"
else
    info "未检测到 NVIDIA GPU，将使用 CPU 模式"
    CUDA_FLAG="-DASR_ENABLE_CUDA=OFF"
fi

mkdir -p "${INSTALL_DIR}"

cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release ${CUDA_FLAG}
cmake --build "${BUILD_DIR}" -j"$(nproc)"

cp "${BUILD_DIR}/asr" "${INSTALL_DIR}/"
info "构建完成: ${INSTALL_DIR}/asr"
