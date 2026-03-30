# =============================================================================
# asr-tool — 本地语音转文字
#
# 说明: 只需编译一份二进制，运行时自动检测最佳加速方案。
#
#  macOS: 编译 CoreML+Metal 支持，运行时检测 CoreML 可用 → 用 CoreML
#          否则回退 CPU（Accelerate 框架）。Intel Mac 仅 CPU。
#
#  Linux/Win: 编译 CUDA 支持，运行时检测 NVIDIA GPU → 用 CUDA
#              否则回退 CPU（OpenBLAS）。
#
# 用法:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j$(nproc)
# =============================================================================

cmake_minimum_required(VERSION 3.18)
project(asr-tool VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 平台检测
if(APPLE)
    set(PLATFORM_NAME "macOS")
elseif(WIN32)
    set(PLATFORM_NAME "Windows")
else()
    set(PLATFORM_NAME "Linux")
endif()

message(STATUS "Building for ${PLATFORM_NAME}")

# 构建选项
option(ASR_ENABLE_COREML "Enable CoreML acceleration (macOS only)" ON)
option(ASR_ENABLE_CUDA "Enable CUDA acceleration (Linux/Windows)" ON)

# ============================================================
# whisper.cpp (FetchContent)
# ============================================================
include(FetchContent)

set(WHISPER_VERSION "v1.8.3")
message(STATUS "Fetching whisper.cpp ${WHISPER_VERSION}...")

FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp
    GIT_TAG ${WHISPER_VERSION}
    GIT_SHALLOW TRUE
)

# whisper.cpp 构建选项
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# macOS: CoreML + Metal
if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    message(STATUS "Enabled GGML_METAL for Apple GPU")

    if(ASR_ENABLE_COREML)
        set(WHISPER_COREML ON CACHE BOOL "" FORCE)
        set(WHISPER_COREML_ALLOW_FALLBACK ON CACHE BOOL "" FORCE)
        message(STATUS "Enabled WHISPER_COREML")
    endif()
endif()

# Linux/Windows: CUDA
if((LINUX OR WIN32) AND ASR_ENABLE_CUDA)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    message(STATUS "Enabled GGML_CUDA for NVIDIA GPU")
endif()

FetchContent_MakeAvailable(whisper)

# ============================================================
# 主可执行文件
# ============================================================
add_executable(asr
    src/main.cpp
    src/platform.cpp
    src/audio.cpp
    src/transcriber.cpp
    src/output.cpp
)

target_include_directories(asr PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(asr PUBLIC whisper)

# macOS 系统框架
if(APPLE)
    target_link_libraries(asr PUBLIC
        "-framework CoreML"
        "-framework Metal"
        "-framework MetalKit"
        "-framework Accelerate"
    )
endif()

# 编译器警告
target_compile_options(asr PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 安装目标
install(TARGETS asr RUNTIME DESTINATION bin)
