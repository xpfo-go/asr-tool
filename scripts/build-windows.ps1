# build-windows.ps1 — Windows 构建脚本 (PowerShell 7+)
# 用法: .\scripts\build-windows.ps1

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$InstallDir = Join-Path $ProjectRoot "bin\windows-x86_64"

function info($msg) { Write-Host "[INFO] $msg" }
function error($msg) { Write-Host "[ERROR] $msg" -ForegroundColor Red }

# 检查依赖
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    error "缺少 cmake，请先安装: choco install cmake"
    exit 1
}

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    error "缺少 ffmpeg，请先安装: choco install ffmpeg"
    exit 1
}

info "开始构建 asr-tool..."

# 检查 CUDA
$CUDA_FLAG = "-DASR_ENABLE_CUDA=OFF"
try {
    $nvidiaSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if ($nvidiaSmi) {
        $output = & nvidia-smi -L 2>$null
        if ($LASTEXITCODE -eq 0) {
            info "检测到 NVIDIA GPU，将启用 CUDA 加速"
            $CUDA_FLAG = "-DASR_ENABLE_CUDA=ON"
        }
    }
} catch {
    info "未检测到 NVIDIA GPU，将使用 CPU 模式"
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

cmake -B $BuildDir -DCMAKE_BUILD_TYPE=Release $CUDA_FLAG
cmake --build $BuildDir --config Release -j (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors

Copy-Item "$BuildDir\asr.exe" $InstallDir
info "构建完成: $InstallDir\asr.exe"
