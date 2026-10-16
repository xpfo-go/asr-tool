# =============================================================================
# asr-tool — 本地语音转文字
#
# 说明: 只需编译一份二进制，运行时自动检测最佳加速方案。
#
#  macOS: 编译 CoreML+Metal 支持，运行时检测 CoreML 可用 → 用 CoreML
#          否则使用 Metal，GPU 不可用时回退 CPU（Accelerate 框架）。
#
#  Linux/Win: 编译 CUDA 支持，运行时检测 NVIDIA GPU → 用 CUDA
#              否则回退 CPU（OpenBLAS）。
#
# 用法:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j$(nproc)
# =============================================================================

cmake_minimum_required(VERSION 3.18)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
project(asr-tool VERSION 2.0.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ASR_TARGET_NAME asr_tool)
set(ASR_BINARY_NAME asr-tool)
set(ASR_LEGACY_BINARY_NAME asr)
set(ASR_APP_VERSION "${PROJECT_VERSION}-rc")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --dirty --always
        OUTPUT_VARIABLE ASR_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(FIND "${ASR_GIT_DESCRIBE}" "v${ASR_APP_VERSION}" ASR_VERSION_PREFIX)
    if(ASR_VERSION_PREFIX EQUAL 0)
        string(REGEX REPLACE "^v" "" ASR_APP_VERSION "${ASR_GIT_DESCRIBE}")
    endif()
endif()
set(ASR_APP_VERSION "${ASR_APP_VERSION}" CACHE INTERNAL "Configured application version" FORCE)

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

if(UNIX AND NOT APPLE)
    set(CMAKE_INSTALL_RPATH "\$ORIGIN;\$ORIGIN/../lib")
endif()

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
include(cmake/sherpa.cmake)

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
    src/meeting.cpp
    src/diarizer.cpp
    src/live.cpp
    src/streaming.cpp
)

if(APPLE)
    target_sources(${ASR_TARGET_NAME} PRIVATE src/live_capture.mm)
    set_source_files_properties(src/live_capture.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
else()
    target_sources(${ASR_TARGET_NAME} PRIVATE src/live_capture.cpp)
endif()

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

target_link_libraries(${ASR_TARGET_NAME} PUBLIC whisper asr_sherpa)
if(ASR_SHERPA_RUNTIME)
    add_custom_command(TARGET ${ASR_TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASR_SHERPA_RUNTIME}
                "$<TARGET_FILE_DIR:${ASR_TARGET_NAME}>"
        COMMAND_EXPAND_LISTS VERBATIM)
    install(FILES ${ASR_SHERPA_RUNTIME} DESTINATION bin)
endif()
if(WIN32)
    foreach(runtime IN ITEMS whisper ggml ggml-base ggml-cpu ggml-cuda)
        if(TARGET ${runtime})
            add_custom_command(TARGET ${ASR_TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${runtime}>"
                        "$<TARGET_FILE_DIR:${ASR_TARGET_NAME}>"
                VERBATIM)
            install(FILES "$<TARGET_FILE:${runtime}>" DESTINATION bin)
        endif()
    endforeach()
endif()

# Keep runtime search path local on Linux so bundled .so files are discovered.
if(UNIX AND NOT APPLE)
    set_target_properties(${ASR_TARGET_NAME} PROPERTIES
        BUILD_RPATH "\$ORIGIN"
        INSTALL_RPATH "\$ORIGIN;\$ORIGIN/../lib"
    )
endif()

# macOS 系统框架
if(APPLE)
    target_link_libraries(${ASR_TARGET_NAME} PUBLIC
        "-framework CoreML"
        "-framework Metal"
        "-framework MetalKit"
        "-framework Accelerate"
        "-framework AVFoundation"
        "-framework CoreAudio"
    )
endif()

# 编译器警告
target_compile_options(${ASR_TARGET_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
)
target_compile_definitions(${ASR_TARGET_NAME} PRIVATE ASR_VERSION="${ASR_APP_VERSION}")

# 安装目标
install(TARGETS ${ASR_TARGET_NAME} RUNTIME DESTINATION bin)
install(FILES docs/THIRD_PARTY_NOTICES.txt DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(fake_ffmpeg tests/fake_ffmpeg.cpp)
    set_target_properties(fake_ffmpeg PROPERTIES OUTPUT_NAME ffmpeg
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/audio-test-tools")
    add_executable(audio_test tests/audio_test.cpp src/audio.cpp)
    target_include_directories(audio_test PRIVATE include)
    foreach(target IN ITEMS fake_ffmpeg audio_test)
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
            $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    endforeach()
    add_dependencies(audio_test fake_ffmpeg)
    add_test(NAME audio COMMAND audio_test "$<TARGET_FILE_DIR:fake_ffmpeg>")
    add_executable(live_test tests/live_test.cpp src/live.cpp src/meeting.cpp src/output.cpp)
    target_include_directories(live_test PRIVATE include)
    target_link_libraries(live_test PRIVATE Threads::Threads)
    target_compile_options(live_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    add_test(NAME live COMMAND live_test)
    set_tests_properties(live PROPERTIES TIMEOUT 15)
    add_executable(live_replay_candidate EXCLUDE_FROM_ALL tests/evaluation/live_replay.cpp
        src/live.cpp src/streaming.cpp src/audio.cpp src/transcriber.cpp src/meeting.cpp
        src/diarizer.cpp src/model_manager.cpp src/output.cpp src/platform.cpp)
    target_include_directories(live_replay_candidate PRIVATE include)
    target_link_libraries(live_replay_candidate PRIVATE whisper asr_sherpa Threads::Threads)
    target_compile_options(live_replay_candidate PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    get_target_property(asr_sources ${ASR_TARGET_NAME} SOURCES)
    get_target_property(asr_libraries ${ASR_TARGET_NAME} LINK_LIBRARIES)
    foreach(candidate IN ITEMS whisper_vad_candidate whisper_no_history_candidate)
        add_executable(${candidate} EXCLUDE_FROM_ALL ${asr_sources})
        target_include_directories(${candidate} PRIVATE include)
        target_link_libraries(${candidate} PRIVATE ${asr_libraries})
        target_compile_options(${candidate} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
            $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
        target_compile_definitions(${candidate} PRIVATE ASR_VERSION="${ASR_APP_VERSION}")
    endforeach()
    target_compile_definitions(whisper_vad_candidate PRIVATE
        ASR_EXPERIMENT_VAD_THRESHOLD=0.3f)
    target_compile_definitions(whisper_no_history_candidate PRIVATE ASR_EXPERIMENT_NO_HISTORY=1)
    foreach(candidate IN ITEMS qwen_candidate qwen_short_candidate sensevoice_candidate
            funasr_nano_candidate funasr_nano_short_candidate funasr_nano_pause_candidate
            firered_ctc_candidate firered_aed_candidate firered_aed_gap_candidate
            firered_aed_restart_candidate)
        add_executable(${candidate} EXCLUDE_FROM_ALL
            tests/evaluation/native_candidate.cpp src/audio.cpp src/meeting.cpp)
        target_include_directories(${candidate} PRIVATE include)
        target_link_libraries(${candidate} PRIVATE whisper asr_sherpa)
        target_compile_options(${candidate} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
            $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    endforeach()
    target_compile_definitions(qwen_short_candidate PRIVATE ASR_CANDIDATE_SHORT_WINDOWS=1)
    target_compile_definitions(sensevoice_candidate PRIVATE ASR_CANDIDATE_SENSEVOICE=1)
    target_compile_definitions(firered_ctc_candidate PRIVATE ASR_CANDIDATE_FIRERED_CTC=1)
    target_compile_definitions(firered_aed_candidate PRIVATE ASR_CANDIDATE_FIRERED_AED=1)
    target_compile_definitions(firered_aed_gap_candidate PRIVATE
        ASR_CANDIDATE_FIRERED_AED=1 ASR_CANDIDATE_GAP_WINDOWS=1)
    target_compile_definitions(firered_aed_restart_candidate PRIVATE
        ASR_CANDIDATE_FIRERED_AED=1 ASR_CANDIDATE_RESTART_WINDOWS=1)
    target_compile_definitions(funasr_nano_candidate PRIVATE ASR_CANDIDATE_FUNASR_NANO=1)
    target_compile_definitions(funasr_nano_short_candidate PRIVATE
        ASR_CANDIDATE_FUNASR_NANO=1 ASR_CANDIDATE_SHORT_WINDOWS=1)
    target_compile_definitions(funasr_nano_pause_candidate PRIVATE
        ASR_CANDIDATE_FUNASR_NANO=1 ASR_CANDIDATE_PAUSE_WINDOWS=1)
    set(ASR_EVALUATION_ONNX_INCLUDE_DIR "" CACHE PATH "ONNX Runtime 1.28.2 headers for native beam experiments")
    set(asr_evaluation_level_filter "dynaudnorm=f=500:g=31:p=0.95:m=10")
    if(APPLE AND ASR_EVALUATION_ONNX_INCLUDE_DIR)
        foreach(profile IN ITEMS beam1 beam3 beam3_level beam3_fp32 beam3_restart)
            if(profile STREQUAL "beam1")
                set(width 1)
            else()
                set(width 3)
            endif()
            set(candidate firered_aed_${profile}_candidate)
            add_executable(${candidate} EXCLUDE_FROM_ALL tests/evaluation/native_candidate.cpp
                tests/evaluation/firered_beam.cpp src/audio.cpp src/meeting.cpp)
            target_include_directories(${candidate} PRIVATE include)
            target_include_directories(${candidate} SYSTEM PRIVATE ${ASR_EVALUATION_ONNX_INCLUDE_DIR})
            target_link_libraries(${candidate} PRIVATE whisper asr_sherpa)
            target_compile_definitions(${candidate} PRIVATE ASR_CANDIDATE_FIRERED_AED=1
                ASR_CANDIDATE_BEAM_SIZE=${width})
            if(profile STREQUAL "beam3_restart")
                target_compile_definitions(${candidate} PRIVATE ASR_CANDIDATE_RESTART_WINDOWS=1)
            else()
                target_compile_definitions(${candidate} PRIVATE ASR_CANDIDATE_GAP_WINDOWS=1)
            endif()
            target_compile_options(${candidate} PRIVATE -Wall -Wextra -Wpedantic)
            if(profile STREQUAL "beam3_level")
                target_compile_definitions(${candidate} PRIVATE
                    ASR_EXPERIMENT_AUDIO_FILTER="${asr_evaluation_level_filter}")
            endif()
            if(profile STREQUAL "beam3_fp32")
                target_compile_definitions(${candidate} PRIVATE ASR_CANDIDATE_FIRERED_FP32=1)
            endif()
        endforeach()
    endif()
    add_executable(token_beam_test tests/evaluation/token_beam_test.cpp)
    target_compile_options(token_beam_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    add_test(NAME token_beam COMMAND token_beam_test)
    add_executable(native_windows_test tests/evaluation/native_windows_test.cpp)
    target_include_directories(native_windows_test PRIVATE include)
    target_compile_options(native_windows_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    add_test(NAME native_windows COMMAND native_windows_test)
    add_executable(sortformer_state_test
        tests/evaluation/sortformer_state_test.cpp tests/evaluation/sortformer_state.cpp)
    target_compile_options(sortformer_state_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    add_test(NAME sortformer_state COMMAND sortformer_state_test)
    add_executable(diarization_candidate EXCLUDE_FROM_ALL
        tests/evaluation/diarization_candidate.cpp src/audio.cpp src/meeting.cpp)
    target_include_directories(diarization_candidate PRIVATE include)
    target_link_libraries(diarization_candidate PRIVATE asr_sherpa)
    target_compile_options(diarization_candidate PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    include(cmake/speaker_experiments.cmake)
    set(ASR_EVALUATION_NEMO_SDK "" CACHE PATH "Developer-only NeMo-Speech SDK prefix")
    if(ASR_EVALUATION_NEMO_SDK)
        find_library(ASR_NEMO_C_LIBRARY NAMES nemo_speech_asr_c
            PATHS "${ASR_EVALUATION_NEMO_SDK}/lib" NO_DEFAULT_PATH REQUIRED)
        add_executable(nemotron_candidate EXCLUDE_FROM_ALL
            tests/evaluation/nemotron_candidate.cpp src/audio.cpp)
        target_include_directories(nemotron_candidate PRIVATE include)
        target_include_directories(nemotron_candidate SYSTEM PRIVATE
            "${ASR_EVALUATION_NEMO_SDK}/include")
        target_link_libraries(nemotron_candidate PRIVATE ${ASR_NEMO_C_LIBRARY})
        target_compile_options(nemotron_candidate PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
            $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    endif()
    foreach(candidate IN ITEMS vad_candidate vad_level_candidate)
        add_executable(${candidate} EXCLUDE_FROM_ALL
            tests/evaluation/vad_candidate.cpp src/audio.cpp src/meeting.cpp src/vad.cpp)
        target_include_directories(${candidate} PRIVATE include)
        target_link_libraries(${candidate} PRIVATE whisper asr_sherpa)
        target_compile_options(${candidate} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
            $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    endforeach()
    target_compile_definitions(vad_level_candidate PRIVATE
        ASR_EXPERIMENT_AUDIO_FILTER="${asr_evaluation_level_filter}")
    add_executable(meeting_test tests/meeting_test.cpp src/meeting.cpp src/output.cpp src/model_manager.cpp)
    target_include_directories(meeting_test PRIVATE include)
    target_compile_options(meeting_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    add_test(NAME meeting COMMAND meeting_test "${CMAKE_CURRENT_BINARY_DIR}/test-output")
    add_test(NAME cli_validation COMMAND ${CMAKE_COMMAND}
        "-DASR_BINARY=$<TARGET_FILE:${ASR_TARGET_NAME}>"
        "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/cli-test-output"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_validation.cmake")
endif()
