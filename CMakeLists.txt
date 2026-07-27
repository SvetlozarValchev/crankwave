cmake_minimum_required(VERSION 3.21)

project(
    engine_sim_offline
    DESCRIPTION "Offline-native engine simulation and audio rendering"
    LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are unsupported; use cmake -S . -B build")
endif()

option(
    ENGINE_SIM_OFFLINE_BUILD_TESTS
    "Build the focused engine-sim-offline test targets"
    "${PROJECT_IS_TOP_LEVEL}"
)
option(
    ENGINE_SIM_OFFLINE_BUILD_CLI
    "Build the engine-sim-offline command-line shell"
    "${PROJECT_IS_TOP_LEVEL}"
)

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(engine_sim_offline_build_options INTERFACE)
add_library(
    engine_sim_offline::build_options
    ALIAS engine_sim_offline_build_options
)

target_compile_features(engine_sim_offline_build_options INTERFACE cxx_std_20)

target_compile_options(
    engine_sim_offline_build_options
    INTERFACE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic;-fno-fast-math;-ffp-contract=off>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/fp:strict;/Zc:__cplusplus>
)

add_library(
    engine_sim_offline_contract
    STATIC
        src/contract/capture.cpp
        src/contract/common.cpp
        src/contract/engine.cpp
        src/contract/parity_model.cpp
        src/contract/presentation.cpp
        src/contract/provenance.cpp
        src/contract/reference_presentation.cpp
        src/contract/render_manifest.cpp
        src/contract/result.cpp
        src/contract/scenario.cpp
        src/contract/source_matrix.cpp
        src/contract/torque.cpp
)
add_library(
    engine_sim_offline::contract
    ALIAS engine_sim_offline_contract
)

target_include_directories(
    engine_sim_offline_contract
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(
    engine_sim_offline_contract
    PUBLIC engine_sim_offline::build_options
)

add_library(
    engine_sim_offline_render
    STATIC
        src/render.cpp
        src/scheduling/render_schedule.cpp
)
add_library(
    engine_sim_offline::render
    ALIAS engine_sim_offline_render
)

target_include_directories(
    engine_sim_offline_render
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_render
    PUBLIC
        engine_sim_offline::contract
        engine_sim_offline::build_options
)

add_library(
    engine_sim_offline_artifacts
    STATIC
        src/artifacts/directory_render_sink.cpp
        src/artifacts/directory_render_sink_commit.cpp
        src/artifacts/directory_render_sink_lifecycle.cpp
        src/artifacts/p18_audition_wav_encoder.cpp
        src/artifacts/secure_filesystem_support.cpp
        src/artifacts/sha256_stream.cpp
        src/artifacts/telemetry_encoder.cpp
        src/artifacts/telemetry_encoder_rows.cpp
        src/artifacts/telemetry_encoder_validation.cpp
        src/artifacts/wav_encoder.cpp
)
add_library(
    engine_sim_offline::artifacts
    ALIAS engine_sim_offline_artifacts
)

target_include_directories(
    engine_sim_offline_artifacts
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/artifacts>
)

target_link_libraries(
    engine_sim_offline_artifacts
    PUBLIC
        engine_sim_offline::contract
        engine_sim_offline::build_options
)

add_library(
    engine_sim_offline_dsp
    STATIC
        src/dsp/p18_fixed_fft.cpp
        src/dsp/p18_pcg32.cpp
        src/dsp/p18_primitives.cpp
        src/dsp/p18_reconstruction_table.cpp
        src/dsp/p18_static_ir_conversion.cpp
)
add_library(
    engine_sim_offline::dsp
    ALIAS engine_sim_offline_dsp
)

target_include_directories(
    engine_sim_offline_dsp
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_dsp
    PUBLIC engine_sim_offline::build_options
)

add_library(
    engine_sim_offline_presentation
    STATIC
        src/presentation/p18_causal_reconstruction.cpp
        src/presentation/p18_conditioning.cpp
        src/presentation/p18_mastering.cpp
        src/presentation/p18_overlap_save_convolver.cpp
        src/presentation/p18_pcm16_ir_decoder.cpp
        src/presentation/p18_source_stage.cpp
)
add_library(
    engine_sim_offline::presentation
    ALIAS engine_sim_offline_presentation
)

target_include_directories(
    engine_sim_offline_presentation
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_presentation
    PUBLIC
        engine_sim_offline::build_options
        engine_sim_offline::contract
        engine_sim_offline::dsp
)

if(ENGINE_SIM_OFFLINE_BUILD_CLI)
    add_library(
        engine_sim_offline_cli_shell
        STATIC
            src/cli/cli_shell.cpp
    )

    target_include_directories(
        engine_sim_offline_cli_shell
        PRIVATE
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/cli>
    )

    target_compile_definitions(
        engine_sim_offline_cli_shell
        PRIVATE ENGINE_SIM_OFFLINE_VERSION_LABEL="development"
    )

    target_link_libraries(
        engine_sim_offline_cli_shell
        PUBLIC engine_sim_offline::build_options
    )

    add_executable(
        engine_sim_offline_cli
        src/cli/main.cpp
    )

    set_target_properties(
        engine_sim_offline_cli
        PROPERTIES OUTPUT_NAME engine-sim-offline
    )

    target_link_libraries(
        engine_sim_offline_cli
        PRIVATE engine_sim_offline_cli_shell
    )
endif()

if(ENGINE_SIM_OFFLINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
