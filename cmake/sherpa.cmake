# Pinned native libraries keep the macOS release self-contained.
set(SHERPA_VERSION "1.13.8")
set(ASR_SHERPA_ROOT "" CACHE PATH "Pre-extracted sherpa-onnx v1.13.8 library archive")
FetchContent_Declare(sherpa_headers
    URL https://github.com/k2-fsa/sherpa-onnx/archive/refs/tags/v${SHERPA_VERSION}.tar.gz
    URL_HASH SHA256=b0374cc56dbc186d442ae73d5de743bb092470b640c4c50ce7b029044c0c4fa8
    SOURCE_SUBDIR headers-only
)
FetchContent_MakeAvailable(sherpa_headers)

if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(SHERPA_PLATFORM osx-arm64-static-no-tts-lib)
        set(SHERPA_HASH 3d7f9b8a496694af13d9802c33b8133231e397bdef302f543d19468765e83136)
    else()
        set(SHERPA_PLATFORM osx-x64-static-no-tts-lib)
        set(SHERPA_HASH 8ffc3ede9f997fec547b5c99b8e2073b8e04fd48a5f65e1a6d5d314ad0106ad9)
    endif()
elseif(WIN32)
    set(SHERPA_PLATFORM win-x64-shared-MD-Release-no-tts-lib)
    set(SHERPA_HASH b90992b888710715d613a4fedcd75d5b4db294ef64f3194e4f77ea0a8b387441)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(SHERPA_PLATFORM linux-x64-shared-no-tts-lib)
    set(SHERPA_HASH bf2d998c8b07012cd5098f3b92673bc1333fd9b927767d7cb664be8190d8bc0b)
else()
    message(FATAL_ERROR "No pinned sherpa-onnx library for this platform")
endif()

if(NOT ASR_SHERPA_ROOT)
    FetchContent_Declare(sherpa_libraries
        URL https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_VERSION}/sherpa-onnx-v${SHERPA_VERSION}-${SHERPA_PLATFORM}.tar.bz2
        URL_HASH SHA256=${SHERPA_HASH}
        SOURCE_SUBDIR prebuilt-only
    )
    FetchContent_MakeAvailable(sherpa_libraries)
    set(ASR_SHERPA_ROOT ${sherpa_libraries_SOURCE_DIR})
endif()

add_library(asr_sherpa INTERFACE)
target_include_directories(asr_sherpa SYSTEM INTERFACE ${sherpa_headers_SOURCE_DIR})
if(APPLE)
    set(SHERPA_ARCHIVES sherpa-onnx-c-api sherpa-onnx-core kaldi-decoder-core
        sherpa-onnx-kaldifst-core sherpa-onnx-fstfar sherpa-onnx-fst
        kaldi-native-fbank-core kissfft-float ssentencepiece_core onnxruntime)
    foreach(library IN LISTS SHERPA_ARCHIVES)
        target_link_libraries(asr_sherpa INTERFACE ${ASR_SHERPA_ROOT}/lib/lib${library}.a)
    endforeach()
    target_link_libraries(asr_sherpa INTERFACE "-framework Foundation" "-framework Accelerate")
elseif(WIN32)
    target_link_libraries(asr_sherpa INTERFACE ${ASR_SHERPA_ROOT}/lib/sherpa-onnx-c-api.lib)
    file(GLOB ASR_SHERPA_RUNTIME ${ASR_SHERPA_ROOT}/lib/*.dll ${ASR_SHERPA_ROOT}/bin/*.dll)
else()
    target_link_libraries(asr_sherpa INTERFACE ${ASR_SHERPA_ROOT}/lib/libsherpa-onnx-c-api.so)
    file(GLOB ASR_SHERPA_RUNTIME ${ASR_SHERPA_ROOT}/lib/*.so*)
endif()
