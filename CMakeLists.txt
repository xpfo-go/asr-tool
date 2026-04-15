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
project(asr-tool VERSION 1.0.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ASR_TARGET_NAME asr_tool)
set(ASR_BINARY_NAME asr-tool)
set(ASR_LEGACY_BINARY_NAME asr)
set(ASR_APP_VERSION "${PROJECT_VERSION}")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --dirty --always
        OUTPUT_VARIABLE ASR_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(ASR_GIT_DESCRIBE)
        string(REGEX REPLACE "^v" "" ASR_APP_VERSION "${ASR_GIT_DESCRIBE}")
    endif()
endif()

message(STATUS "asr-tool version: ${ASR_APP_VERSION}")

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
if(((CMAKE_SYSTEM_NAME STREQUAL "Linux") OR WIN32) AND ASR_ENABLE_CUDA)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    message(STATUS "Enabled GGML_CUDA for NVIDIA GPU")
endif()

# Linux/Windows: use dynamic backend loading so CUDA can be optional at runtime
# (load CUDA backend when available, fallback to CPU when unavailable).
if((CMAKE_SYSTEM_NAME STREQUAL "Linux") OR WIN32)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    set(GGML_BACKEND_DL ON CACHE BOOL "" FORCE)
    # GGML_BACKEND_DL is incompatible with GGML_NATIVE in current ggml.
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(whisper)

# ============================================================
# 主可执行文件
# ============================================================
add_executable(${ASR_TARGET_NAME}
    src/main.cpp
    src/model_manager.cpp
    src/platform.cpp
    src/audio.cpp
    src/transcriber.cpp
    src/output.cpp
)

set_target_properties(${ASR_TARGET_NAME} PROPERTIES
    OUTPUT_NAME "${ASR_BINARY_NAME}"
)

# Clean up the legacy binary name in reused build directories.
add_custom_command(TARGET ${ASR_TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:${ASR_TARGET_NAME}>/${ASR_LEGACY_BINARY_NAME}$<TARGET_FILE_SUFFIX:${ASR_TARGET_NAME}>"
    VERBATIM
)

target_include_directories(${ASR_TARGET_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${ASR_TARGET_NAME} PUBLIC whisper)

# Keep runtime search path local on Linux so bundled .so files are discovered.
if(UNIX AND NOT APPLE)
    set_target_properties(${ASR_TARGET_NAME} PROPERTIES
        BUILD_RPATH "\$ORIGIN"
        INSTALL_RPATH "\$ORIGIN"
    )
endif()

# macOS 系统框架
if(APPLE)
    target_link_libraries(${ASR_TARGET_NAME} PUBLIC
        "-framework CoreML"
        "-framework Metal"
        "-framework MetalKit"
        "-framework Accelerate"
    )
endif()

# 编译器警告
target_compile_options(${ASR_TARGET_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
target_compile_definitions(${ASR_TARGET_NAME} PRIVATE ASR_VERSION="${ASR_APP_VERSION}")

# 安装目标
install(TARGETS ${ASR_TARGET_NAME} RUNTIME DESTINATION bin)
