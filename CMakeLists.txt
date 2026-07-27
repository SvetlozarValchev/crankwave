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
option(
    ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS
    "Build isolated local-evaluation tools that can read frozen reference fixtures"
    OFF
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

include(cmake/RendererNumericPolicy.cmake)
engine_sim_offline_define_renderer_numeric_policy()

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
        src/contract/sha256_stream.cpp
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
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_contract)

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
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_render)

add_library(
    engine_sim_offline_artifacts
    STATIC
        src/artifacts/canonical_json_writer.cpp
        src/artifacts/directory_render_sink.cpp
        src/artifacts/directory_render_sink_commit.cpp
        src/artifacts/directory_render_sink_lifecycle.cpp
        src/artifacts/p18_audition_wav_encoder.cpp
        src/artifacts/reference_manifest_encoder.cpp
        src/artifacts/reference_manifest_encoder_presentation.cpp
        src/artifacts/reference_manifest_encoder_reference.cpp
        src/artifacts/secure_filesystem_support.cpp
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
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/artifacts>
)

target_link_libraries(
    engine_sim_offline_artifacts
    PUBLIC
        engine_sim_offline::contract
        engine_sim_offline::build_options
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_artifacts)

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
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_dsp)

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
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_presentation)

if(ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS OR ENGINE_SIM_OFFLINE_BUILD_TESTS)
    include(cmake/RendererSourceStamp.cmake)

    add_library(
        engine_sim_offline_execution
        STATIC
            src/execution/linux_execution_facts.cpp
    )

    target_include_directories(
        engine_sim_offline_execution
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_execution
        PUBLIC engine_sim_offline::contract
        PRIVATE engine_sim_offline::build_options
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_execution
    )

    add_library(
        engine_sim_offline_renderer_determinism
        STATIC
            src/determinism/loaded_runtime_identity.cpp
            src/determinism/renderer_determinism_envelope.cpp
            src/determinism/renderer_numeric_environment.cpp
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        target_sources(
            engine_sim_offline_renderer_determinism
            PRIVATE
                src/determinism/linux_loaded_provider.cpp
                src/determinism/linux_loaded_provider_file.cpp
        )
    endif()
    engine_sim_offline_add_renderer_source_stamp(
        engine_sim_offline_renderer_determinism
    )

    target_include_directories(
        engine_sim_offline_renderer_determinism
        PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_renderer_determinism
        PUBLIC engine_sim_offline::contract
        PRIVATE
            engine_sim_offline::build_options
            ${CMAKE_DL_LIBS}
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_renderer_determinism
    )

    add_library(
        engine_sim_offline_p18_presentation_session
        STATIC
            src/reference/p18_reference_render_session.cpp
    )

    target_include_directories(
        engine_sim_offline_p18_presentation_session
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_p18_presentation_session
        PUBLIC
            engine_sim_offline::artifacts
            engine_sim_offline_execution
            engine_sim_offline::presentation
        PRIVATE engine_sim_offline::build_options
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_p18_presentation_session
    )

    add_library(
        engine_sim_offline_p18_reference_catalog
        STATIC
            src/reference/p18_reference_catalog.cpp
    )

    target_include_directories(
        engine_sim_offline_p18_reference_catalog
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_p18_reference_catalog
        PUBLIC
            engine_sim_offline::contract
            engine_sim_offline::build_options
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_p18_reference_catalog
    )

    add_library(
        engine_sim_offline_p18_reference_fixture
        STATIC
            src/reference/p18_reference_audit_reader.cpp
            src/reference/p18_reference_fixture_loader.cpp
            src/reference/p18_reference_seed_reader.cpp
    )

    target_include_directories(
        engine_sim_offline_p18_reference_fixture
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_p18_reference_fixture
        PUBLIC
            engine_sim_offline::artifacts
            engine_sim_offline::presentation
            engine_sim_offline_p18_reference_catalog
        PRIVATE engine_sim_offline::build_options
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_p18_reference_fixture
    )

    add_library(
        engine_sim_offline_p18_reference_session_adapter
        STATIC
            src/reference/p18_reference_session_adapter.cpp
    )

    target_include_directories(
        engine_sim_offline_p18_reference_session_adapter
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_p18_reference_session_adapter
        PUBLIC
            engine_sim_offline_p18_presentation_session
            engine_sim_offline_p18_reference_fixture
        PRIVATE
            engine_sim_offline::build_options
            engine_sim_offline_p18_reference_catalog
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_p18_reference_session_adapter
    )

    add_library(
        engine_sim_offline_p18_reference_provenance
        STATIC
            src/reference/p18_reference_method_identity.cpp
            src/reference/p18_reference_provenance.cpp
    )

    target_include_directories(
        engine_sim_offline_p18_reference_provenance
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_p18_reference_provenance
        PUBLIC
            engine_sim_offline::contract
            engine_sim_offline_p18_reference_catalog
            engine_sim_offline_p18_reference_fixture
        PRIVATE
            engine_sim_offline::build_options
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_p18_reference_provenance
    )

    add_library(
        engine_sim_offline_p18_reference_manifest
        STATIC
            src/reference/p18_reference_manifest_completion.cpp
            src/reference/p18_reference_manifest_content.cpp
    )

    target_include_directories(
        engine_sim_offline_p18_reference_manifest
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_p18_reference_manifest
        PUBLIC
            engine_sim_offline_execution
            engine_sim_offline_p18_presentation_session
            engine_sim_offline_p18_reference_provenance
            engine_sim_offline_renderer_determinism
        PRIVATE
            engine_sim_offline::artifacts
            engine_sim_offline::build_options
            engine_sim_offline_p18_reference_catalog
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_p18_reference_manifest
    )
endif()

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

if(ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS)
    add_executable(
        engine_sim_offline_p18_reference_render
        src/reference/p18_reference_main.cpp
    )

    set_target_properties(
        engine_sim_offline_p18_reference_render
        PROPERTIES OUTPUT_NAME engine-sim-offline-p18-reference-render
    )

    target_include_directories(
        engine_sim_offline_p18_reference_render
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )

    target_link_options(
        engine_sim_offline_p18_reference_render
        PRIVATE $<$<PLATFORM_ID:Linux>:LINKER:--no-as-needed>
    )

    target_link_libraries(
        engine_sim_offline_p18_reference_render
        PRIVATE
            engine_sim_offline::artifacts
            engine_sim_offline_execution
            engine_sim_offline_p18_reference_catalog
            engine_sim_offline_p18_reference_fixture
            engine_sim_offline_p18_reference_manifest
            engine_sim_offline_p18_reference_session_adapter
            engine_sim_offline_p18_presentation_session
            engine_sim_offline::presentation
            engine_sim_offline_renderer_determinism
            $<$<PLATFORM_ID:Linux>:m>
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_p18_reference_render
    )
    engine_sim_offline_finalize_renderer_numeric_policy(
        engine_sim_offline_p18_reference_render
    )
endif()

set(
    _engine_sim_offline_numeric_policy_targets
    engine_sim_offline_contract
    engine_sim_offline_render
    engine_sim_offline_artifacts
    engine_sim_offline_dsp
    engine_sim_offline_presentation
)
if(ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS OR ENGINE_SIM_OFFLINE_BUILD_TESTS)
    list(
        APPEND
        _engine_sim_offline_numeric_policy_targets
        engine_sim_offline_execution
        engine_sim_offline_renderer_determinism
        engine_sim_offline_p18_presentation_session
        engine_sim_offline_p18_reference_catalog
        engine_sim_offline_p18_reference_fixture
        engine_sim_offline_p18_reference_manifest
        engine_sim_offline_p18_reference_provenance
        engine_sim_offline_p18_reference_session_adapter
    )
endif()
if(ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS)
    list(
        APPEND
        _engine_sim_offline_numeric_policy_targets
        engine_sim_offline_p18_reference_render
    )
endif()
foreach(_target IN LISTS _engine_sim_offline_numeric_policy_targets)
    engine_sim_offline_assert_renderer_numeric_policy("${_target}")
endforeach()
if(ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS)
    engine_sim_offline_assert_final_renderer_numeric_policy(
        engine_sim_offline_p18_reference_render
    )
endif()
unset(_engine_sim_offline_numeric_policy_targets)

if(ENGINE_SIM_OFFLINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
