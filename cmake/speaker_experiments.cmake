option(ASR_BUILD_SPECTRAL_EXPERIMENT "Build the developer-only native spectral comparison" OFF)
if(ASR_BUILD_SPECTRAL_EXPERIMENT)
    FetchContent_Declare(asr_eigen
        URL https://gitlab.com/libeigen/eigen/-/archive/5.0.1/eigen-5.0.1.tar.gz
        URL_HASH SHA256=e9c326dc8c05cd1e044c71f30f1b2e34a6161a3b6ecf445d56b53ff1669e3dec
        SOURCE_SUBDIR headers-only)
    FetchContent_MakeAvailable(asr_eigen)
    FetchContent_Declare(asr_hclust
        URL https://codeload.github.com/csukuangfj/hclust-cpp/tar.gz/refs/tags/2026-02-25
        URL_HASH SHA256=8f14e024c709d73afb40ae69cb22de4b73dba67cbce40f2e518813da8139ab56
        SOURCE_SUBDIR headers-only)
    FetchContent_MakeAvailable(asr_hclust)
    add_library(spectral_clustering STATIC EXCLUDE_FROM_ALL tests/evaluation/spectral_clustering.cpp)
    target_include_directories(spectral_clustering SYSTEM PRIVATE ${asr_eigen_SOURCE_DIR} ${asr_hclust_SOURCE_DIR})
    target_compile_definitions(spectral_clustering PRIVATE EIGEN_MPL2_ONLY EIGEN_DONT_PARALLELIZE)
    target_compile_options(spectral_clustering PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>)
    add_executable(spectral_clustering_test tests/evaluation/spectral_clustering_test.cpp)
    target_link_libraries(spectral_clustering_test PRIVATE spectral_clustering)
    add_test(NAME spectral_clustering COMMAND spectral_clustering_test)
    add_executable(overlap_masks_test tests/evaluation/overlap_masks_test.cpp)
    target_include_directories(overlap_masks_test SYSTEM PRIVATE ${asr_eigen_SOURCE_DIR})
    target_compile_definitions(overlap_masks_test PRIVATE EIGEN_MPL2_ONLY)
    add_test(NAME overlap_masks COMMAND overlap_masks_test)
    add_executable(powerset_decode_test tests/evaluation/powerset_decode_test.cpp)
    target_include_directories(powerset_decode_test SYSTEM PRIVATE ${asr_eigen_SOURCE_DIR})
    target_compile_definitions(powerset_decode_test PRIVATE EIGEN_MPL2_ONLY)
    add_test(NAME powerset_decode COMMAND powerset_decode_test)
    add_executable(constrained_assignment_test tests/evaluation/constrained_assignment_test.cpp)
    target_include_directories(constrained_assignment_test SYSTEM PRIVATE ${asr_eigen_SOURCE_DIR})
    target_compile_definitions(constrained_assignment_test PRIVATE EIGEN_MPL2_ONLY)
    add_test(NAME constrained_assignment COMMAND constrained_assignment_test)
    # Only the pinned macOS static archive supports this isolated substitution.
    if(APPLE)
        foreach(candidate IN ITEMS diarization_spectral_candidate diarization_nme_candidate
                diarization_nme_turns_candidate diarization_hybrid_candidate
                diarization_hybrid_dense_candidate)
            add_executable(${candidate} EXCLUDE_FROM_ALL
                tests/evaluation/diarization_candidate.cpp tests/evaluation/spectral_adapter.cpp
                src/audio.cpp src/meeting.cpp)
            target_include_directories(${candidate} PRIVATE include)
            target_link_libraries(${candidate} PRIVATE spectral_clustering asr_sherpa)
            target_compile_definitions(${candidate} PRIVATE ASR_CANDIDATE_SPECTRAL=1)
            target_compile_options(${candidate} PRIVATE -Wall -Wextra -Wpedantic)
        endforeach()
        target_compile_definitions(diarization_nme_candidate PRIVATE ASR_CANDIDATE_NME=1)
        target_compile_definitions(diarization_hybrid_candidate PRIVATE ASR_CANDIDATE_HYBRID=1)
        target_compile_definitions(diarization_hybrid_dense_candidate PRIVATE
            ASR_CANDIDATE_HYBRID=1 ASR_CANDIDATE_DENSE_WINDOWS=1)
        target_compile_definitions(diarization_nme_turns_candidate PRIVATE
            ASR_CANDIDATE_NME=1 ASR_CANDIDATE_SHORT_TURNS=1)
        if(ASR_EVALUATION_ONNX_INCLUDE_DIR)
            set(ASR_SORTFORMER_ONNX_LIBRARY "" CACHE FILEPATH
                "ONNX Runtime library for the developer-only Sortformer candidate")
            foreach(candidate IN ITEMS sortformer_candidate sortformer_file_candidate)
                add_executable(${candidate} EXCLUDE_FROM_ALL
                    tests/evaluation/sortformer_candidate.cpp tests/evaluation/sortformer_state.cpp
                    src/audio.cpp)
                target_include_directories(${candidate} PRIVATE include)
                target_link_libraries(${candidate} PRIVATE "-framework Accelerate")
                target_include_directories(${candidate} SYSTEM PRIVATE
                    ${ASR_EVALUATION_ONNX_INCLUDE_DIR})
                if(ASR_SORTFORMER_ONNX_LIBRARY)
                    target_link_libraries(${candidate} PRIVATE ${ASR_SORTFORMER_ONNX_LIBRARY})
                else()
                    target_link_libraries(${candidate} PRIVATE asr_sherpa)
                endif()
                target_compile_options(${candidate} PRIVATE -Wall -Wextra -Wpedantic)
            endforeach()
            target_compile_definitions(sortformer_file_candidate PRIVATE ASR_SORTFORMER_FILE_PROFILE=1)
            FetchContent_Declare(asr_kaldi_headers
                URL https://codeload.github.com/k2-fsa/kaldi-decoder/tar.gz/refs/tags/v0.3.0
                URL_HASH SHA256=b9f34cfb4fd3b1344100eead79ef4d37aa15962274b9e3056de345021f76a1b0
                SOURCE_SUBDIR headers-only)
            FetchContent_Declare(asr_kaldifst_headers
                URL https://codeload.github.com/k2-fsa/kaldifst/tar.gz/refs/tags/v1.8.0
                URL_HASH SHA256=3f247b7e5a2409071202f5e2bc6200060f66728c0a3443c03923ad2723e040b3
                SOURCE_SUBDIR headers-only)
            FetchContent_Declare(asr_openfst_headers
                URL https://codeload.github.com/csukuangfj/openfst/tar.gz/refs/tags/v1.8.5-2026-07-09
                URL_HASH SHA256=2ff712a32952fcb01d351121a6bc8ccf4fdc6b2aa06ce8df2b3095dedd518c0e
                SOURCE_SUBDIR headers-only)
            FetchContent_MakeAvailable(asr_kaldi_headers asr_kaldifst_headers asr_openfst_headers)
            set(pipeline_header sherpa-onnx/csrc/offline-speaker-diarization-pyannote-impl.h)
            file(SHA256 "${sherpa_headers_SOURCE_DIR}/${pipeline_header}" pipeline_hash)
            if(NOT pipeline_hash STREQUAL "c3d1ad13e0c893abc12ad422d1c84efb53b886269579104fbce9545b869c5fef")
                message(FATAL_ERROR "Overlap experiment requires the unmodified pinned sherpa pipeline")
            endif()
            function(asr_add_pipeline_probe candidate override_directory)
                add_executable(${candidate} EXCLUDE_FROM_ALL
                    tests/evaluation/diarization_candidate.cpp tests/evaluation/spectral_adapter.cpp
                    tests/evaluation/overlap_pipeline.cpp src/audio.cpp src/meeting.cpp)
                target_include_directories(${candidate} PRIVATE include "${override_directory}")
                target_include_directories(${candidate} SYSTEM PRIVATE
                    ${asr_eigen_SOURCE_DIR} ${ASR_EVALUATION_ONNX_INCLUDE_DIR}
                    ${asr_kaldi_headers_SOURCE_DIR} ${asr_kaldifst_headers_SOURCE_DIR}
                    ${asr_openfst_headers_SOURCE_DIR}/src/include)
                target_link_libraries(${candidate} PRIVATE spectral_clustering asr_sherpa)
                target_compile_definitions(${candidate} PRIVATE
                    ASR_CANDIDATE_SPECTRAL=1 EIGEN_MPL2_ONLY EIGEN_DONT_PARALLELIZE ${ARGN})
                target_compile_options(${candidate} PRIVATE -Wall -Wextra -Wpedantic)
            endfunction()
            file(READ "${sherpa_headers_SOURCE_DIR}/${pipeline_header}" pipeline_source)
            set(overlap_fallback [=[auto new_labels = ExcludeOverlap(labels);
    int restored = 0;
    for (size_t i = 0; i < labels.size(); ++i) restored += restore_overlap_masks(labels[i], new_labels[i]);
    std::fprintf(stderr, "[OVERLAP] restored_local_speaker_masks=%d\n", restored);]=])
            string(REPLACE "auto new_labels = ExcludeOverlap(labels);"
                "${overlap_fallback}"
                pipeline_source "${pipeline_source}")
            set(pipeline_override "${CMAKE_CURRENT_BINARY_DIR}/evaluation-overlap")
            file(MAKE_DIRECTORY "${pipeline_override}/sherpa-onnx/csrc")
            file(WRITE "${pipeline_override}/${pipeline_header}" "${pipeline_source}")
            asr_add_pipeline_probe(diarization_nme_overlap_candidate "${pipeline_override}"
                ASR_CANDIDATE_NME=1 ASR_CANDIDATE_OVERLAP_MASKS=1)

            file(READ "${sherpa_headers_SOURCE_DIR}/${pipeline_header}" marginal_source)
            string(REPLACE "labels.push_back(ToMultiLabel(m));"
                "labels.push_back(minimum_risk_powerset_labels(m, powerset_mapping_));"
                marginal_source "${marginal_source}")
            set(marginal_override "${CMAKE_CURRENT_BINARY_DIR}/evaluation-marginal")
            file(MAKE_DIRECTORY "${marginal_override}/sherpa-onnx/csrc")
            file(WRITE "${marginal_override}/${pipeline_header}" "${marginal_source}")
            asr_add_pipeline_probe(diarization_hybrid_marginal_candidate "${marginal_override}"
                ASR_CANDIDATE_HYBRID=1 ASR_CANDIDATE_MARGINAL=1)

            file(READ "${sherpa_headers_SOURCE_DIR}/${pipeline_header}" constrained_source)
            set(constrained_assignment [=[auto reassigned = constrained_centroid_assignment(
        embeddings, chunk_speaker_samples_list_pair.first, cluster_labels);
    int changed = 0, abstained = 0;
    for (size_t i = 0; i < reassigned.size(); ++i) {
      changed += reassigned[i] != cluster_labels[i];
      abstained += reassigned[i] < 0;
    }
    std::fprintf(stderr, "[ASSIGNMENT] changed=%d abstained=%d\n", changed, abstained);
    cluster_labels = std::move(reassigned);
    int32_t max_cluster_index =]=])
            string(REPLACE "int32_t max_cluster_index =" "${constrained_assignment}"
                constrained_source "${constrained_source}")
            set(constrained_override "${CMAKE_CURRENT_BINARY_DIR}/evaluation-constrained")
            file(MAKE_DIRECTORY "${constrained_override}/sherpa-onnx/csrc")
            file(WRITE "${constrained_override}/${pipeline_header}" "${constrained_source}")
            asr_add_pipeline_probe(diarization_hybrid_constrained_candidate "${constrained_override}"
                ASR_CANDIDATE_HYBRID=1 ASR_CANDIDATE_CONSTRAINED=1)
        endif()
    endif()
endif()
