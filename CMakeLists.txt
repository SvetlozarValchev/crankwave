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
        src/contract/exhaust_acoustics.cpp
        src/contract/held_speed_operating_point_result.cpp
        src/contract/parity_model.cpp
        src/contract/presentation.cpp
        src/contract/provenance.cpp
        src/contract/provenance_digest.cpp
        src/contract/randomness.cpp
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
    engine_sim_offline_request_identity
    STATIC
        src/identity/canonical_json_writer.cpp
        src/identity/simulation_request_identity.cpp
        src/identity/simulation_request_identity_engine.cpp
        src/identity/simulation_request_identity_scenario.cpp
        src/identity/simulation_request_identity_writer.cpp
)
add_library(
    engine_sim_offline::request_identity
    ALIAS engine_sim_offline_request_identity
)

target_include_directories(
    engine_sim_offline_request_identity
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_request_identity
    PUBLIC
        engine_sim_offline::contract
        engine_sim_offline::build_options
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_request_identity
)

add_library(
    engine_sim_offline_profiles
    STATIC
        src/profiles/bmw_m52b28_operating_profile.cpp
        src/profiles/bmw_m52b28_operating_profile_validation.cpp
        src/profiles/bmw_m52b28_low_order_engine.cpp
        src/profiles/bmw_m52b28_profile_provenance.cpp
        src/profiles/bmw_m52b28_full_throttle_torque_sweep_request.cpp
        src/profiles/bmw_m52b28_full_throttle_torque_sweep_scenario.cpp
        src/profiles/bmw_m52b28_full_throttle_torque_sweep_validation.cpp
        src/profiles/bmw_m52b28_held_idle_low_load_request.cpp
        src/profiles/bmw_m52b28_held_idle_low_load_scenario.cpp
        src/profiles/bmw_m52b28_held_idle_low_load_validation.cpp
        src/profiles/bmw_m52b28_held_regression_request.cpp
        src/profiles/bmw_m52b28_held_regression_scenario.cpp
        src/profiles/bmw_m52b28_held_regression_validation.cpp
        src/profiles/bmw_m52b28_inertial_dyno_listening_request.cpp
        src/profiles/bmw_m52b28_inertial_dyno_listening_scenario.cpp
        src/profiles/bmw_m52b28_inertial_dyno_listening_validation.cpp
        src/profiles/bmw_m52b28_parity_request.cpp
        src/profiles/bmw_m52b28_parity_scenario.cpp
        src/profiles/bmw_m52b28_parity_validation.cpp
)
add_library(
    engine_sim_offline::profiles
    ALIAS engine_sim_offline_profiles
)

target_include_directories(
    engine_sim_offline_profiles
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_profiles
    PUBLIC
        engine_sim_offline::contract
        engine_sim_offline::build_options
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_profiles)

add_library(
    engine_sim_offline_simulation
    STATIC
        src/simulation/fixed_horizon_cycle_sampling.cpp
        src/simulation/chen_flynn_cycle_mean_loss.cpp
        src/simulation/cycle_accounting_method_descriptors.cpp
        src/simulation/cycle_accounting_method_registry.cpp
        src/simulation/four_stroke_cycle_integrator.cpp
        src/simulation/legacy_combustion_primitives.cpp
        src/simulation/legacy_fixed_crank_torque_accounting.cpp
        src/simulation/legacy_fixed_valvetrain.cpp
        src/simulation/legacy_gas_primitives.cpp
        src/simulation/legacy_ignition_schedule.cpp
        src/simulation/inertial_crank_dynamics.cpp
        src/simulation/inertial_dyno_method_registry.cpp
        src/simulation/legacy_low_order_gas.cpp
        src/simulation/legacy_low_order_gas_compile.cpp
        src/simulation/low_order_capture_buffer.cpp
        src/simulation/legacy_low_order_mechanics.cpp
        src/simulation/legacy_low_order_mechanics_compile.cpp
        src/simulation/low_order_capture_session.cpp
        src/simulation/low_order_capture_session_compile.cpp
        src/simulation/legacy_mechanics_primitives.cpp
        src/simulation/kinematic_scenario_schedule.cpp
        src/simulation/low_order_capture_plan.cpp
        src/simulation/low_order_engine_core_v1_runtime.cpp
        src/simulation/low_order_inertial_dyno_v1_runtime.cpp
        src/simulation/low_order_inertial_dyno_v1_runtime_compile.cpp
        src/simulation/low_order_operating_point_v1_runtime.cpp
        src/simulation/low_order_operating_point_v1_runtime_compile.cpp
        src/simulation/operating_cycle_accountant.cpp
)
add_library(
    engine_sim_offline::simulation
    ALIAS engine_sim_offline_simulation
)

target_include_directories(
    engine_sim_offline_simulation
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_simulation
    PUBLIC
        engine_sim_offline::contract
        engine_sim_offline::build_options
    PRIVATE engine_sim_offline::dsp
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_simulation)

target_link_libraries(
    engine_sim_offline_profiles
    PRIVATE engine_sim_offline_simulation
)

add_library(
    engine_sim_offline_render
    STATIC
        src/render.cpp
        src/render/compiled_presentation_job.cpp
        src/render/compiled_presentation_job_execute.cpp
        src/render/render_job_derivation.cpp
        src/render/render_job_failure.cpp
        src/render/render_request.cpp
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
    PRIVATE
        engine_sim_offline::request_identity
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_render)

add_library(
    engine_sim_offline_artifacts
    STATIC
        src/artifacts/directory_render_sink.cpp
        src/artifacts/directory_render_sink_commit.cpp
        src/artifacts/directory_render_sink_lifecycle.cpp
        src/artifacts/audition_wav_encoder.cpp
        src/artifacts/manifest_encoder.cpp
        src/artifacts/manifest_encoder_output.cpp
        src/artifacts/manifest_encoder_presentation.cpp
        src/artifacts/secure_filesystem_support.cpp
        src/artifacts/simulation_manifest_encoder.cpp
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
    PRIVATE
        engine_sim_offline::request_identity
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_artifacts)

add_library(
    engine_sim_offline_dsp
    STATIC
        src/dsp/causal_reconstruction_table.cpp
        src/dsp/fixed_fft.cpp
        src/dsp/pcg32.cpp
        src/dsp/source_conditioning_primitives.cpp
        src/dsp/static_ir_conversion.cpp
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
        src/presentation/causal_reconstruction.cpp
        src/presentation/exhaust_source_stage.cpp
        src/presentation/mastering.cpp
        src/presentation/overlap_save_convolver.cpp
        src/presentation/pcm16_ir_decoder.cpp
        src/presentation/presentation_asset_compiler.cpp
        src/presentation/presentation_calibration_compiler.cpp
        src/presentation/presentation_method_descriptors_asset.cpp
        src/presentation/presentation_method_descriptors_output.cpp
        src/presentation/presentation_method_descriptors_source.cpp
        src/presentation/presentation_method_registry.cpp
        src/presentation/route_conditioning.cpp
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

add_library(
    engine_sim_offline_excitation
    STATIC
        src/excitation/captured_exhaust_excitation.cpp
        src/excitation/captured_exhaust_excitation_compile.cpp
)
add_library(
    engine_sim_offline::excitation
    ALIAS engine_sim_offline_excitation
)

target_include_directories(
    engine_sim_offline_excitation
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_excitation
    PUBLIC
        engine_sim_offline::contract
    PRIVATE engine_sim_offline::build_options
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_excitation)

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
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_execution)

add_library(
    engine_sim_offline_presentation_session
    STATIC
        src/presentation/presentation_render_session.cpp
)
add_library(
    engine_sim_offline::presentation_session
    ALIAS engine_sim_offline_presentation_session
)

target_include_directories(
    engine_sim_offline_presentation_session
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_presentation_session
    PUBLIC
        engine_sim_offline::artifacts
        engine_sim_offline_execution
        engine_sim_offline::presentation
    PRIVATE engine_sim_offline::build_options
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_presentation_session
)

include(cmake/RendererSourceStamp.cmake)

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

target_link_libraries(
    engine_sim_offline_render
    PRIVATE
        engine_sim_offline::simulation
        engine_sim_offline::excitation
        engine_sim_offline::presentation
        engine_sim_offline::presentation_session
        engine_sim_offline_renderer_determinism
)

if(ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS OR ENGINE_SIM_OFFLINE_BUILD_TESTS)
    add_library(
        engine_sim_offline_reference_parity_v1_reader
        STATIC
            src/reference/reference_parity_v1_reader.cpp
    )

    target_include_directories(
        engine_sim_offline_reference_parity_v1_reader
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_reference_parity_v1_reader
        PUBLIC engine_sim_offline::build_options
        PRIVATE engine_sim_offline::contract
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_reference_parity_v1_reader
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
        engine_sim_offline_bmw_p18_render_specification
        STATIC
            src/reference/bmw_p18_render_specification.cpp
    )

    target_include_directories(
        engine_sim_offline_bmw_p18_render_specification
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_bmw_p18_render_specification
        PUBLIC
            engine_sim_offline::contract
            engine_sim_offline::build_options
        PRIVATE
            engine_sim_offline::presentation
            engine_sim_offline_p18_reference_catalog
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_bmw_p18_render_specification
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
        engine_sim_offline_m4_bmw_torque_sweep_evidence
        STATIC
            src/reference/bmw_m52b28_torque_sweep_evidence.cpp
            src/reference/bmw_m52b28_torque_sweep_evidence_encoder.cpp
    )

    target_include_directories(
        engine_sim_offline_m4_bmw_torque_sweep_evidence
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        engine_sim_offline_m4_bmw_torque_sweep_evidence
        PUBLIC
            engine_sim_offline::contract
            engine_sim_offline::build_options
        PRIVATE
            engine_sim_offline::profiles
            engine_sim_offline::request_identity
            engine_sim_offline::simulation
            engine_sim_offline_renderer_determinism
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_m4_bmw_torque_sweep_evidence
    )

endif()

if(ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS AND
   CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(
        engine_sim_offline_m4_bmw_listening
        src/reference/m4_bmw_listening_main.cpp
    )

    set_target_properties(
        engine_sim_offline_m4_bmw_listening
        PROPERTIES OUTPUT_NAME engine-sim-offline-m4-bmw-listening
    )

    target_include_directories(
        engine_sim_offline_m4_bmw_listening
        PRIVATE
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_options(
        engine_sim_offline_m4_bmw_listening
        PRIVATE LINKER:--no-as-needed
    )

    target_link_libraries(
        engine_sim_offline_m4_bmw_listening
        PRIVATE
            engine_sim_offline::artifacts
            engine_sim_offline::presentation
            engine_sim_offline::profiles
            engine_sim_offline::render
            engine_sim_offline_bmw_p18_render_specification
            engine_sim_offline_p18_reference_catalog
            engine_sim_offline_p18_reference_fixture
            m
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_m4_bmw_listening
    )
    engine_sim_offline_finalize_renderer_numeric_policy(
        engine_sim_offline_m4_bmw_listening
    )

    add_executable(
        engine_sim_offline_m4_bmw_torque_sweep
        src/reference/m4_bmw_torque_sweep_main.cpp
    )

    set_target_properties(
        engine_sim_offline_m4_bmw_torque_sweep
        PROPERTIES OUTPUT_NAME engine-sim-offline-m4-bmw-torque-sweep
    )

    target_include_directories(
        engine_sim_offline_m4_bmw_torque_sweep
        PRIVATE
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_options(
        engine_sim_offline_m4_bmw_torque_sweep
        PRIVATE LINKER:--no-as-needed
    )

    target_link_libraries(
        engine_sim_offline_m4_bmw_torque_sweep
        PRIVATE
            engine_sim_offline::artifacts
            engine_sim_offline::profiles
            engine_sim_offline_m4_bmw_torque_sweep_evidence
            engine_sim_offline_renderer_determinism
            m
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_m4_bmw_torque_sweep
    )
    engine_sim_offline_finalize_renderer_numeric_policy(
        engine_sim_offline_m4_bmw_torque_sweep
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

set(
    _engine_sim_offline_numeric_policy_targets
    engine_sim_offline_contract
    engine_sim_offline_profiles
    engine_sim_offline_simulation
    engine_sim_offline_render
    engine_sim_offline_artifacts
    engine_sim_offline_dsp
    engine_sim_offline_presentation
    engine_sim_offline_excitation
    engine_sim_offline_execution
    engine_sim_offline_presentation_session
    engine_sim_offline_renderer_determinism
)
if(ENGINE_SIM_OFFLINE_BUILD_REFERENCE_TOOLS OR ENGINE_SIM_OFFLINE_BUILD_TESTS)
    list(
        APPEND
        _engine_sim_offline_numeric_policy_targets
        engine_sim_offline_reference_parity_v1_reader
        engine_sim_offline_p18_reference_catalog
        engine_sim_offline_bmw_p18_render_specification
        engine_sim_offline_p18_reference_fixture
        engine_sim_offline_m4_bmw_torque_sweep_evidence
    )
endif()
if(TARGET engine_sim_offline_m4_bmw_listening)
    list(
        APPEND
        _engine_sim_offline_numeric_policy_targets
        engine_sim_offline_m4_bmw_listening
    )
endif()
if(TARGET engine_sim_offline_m4_bmw_torque_sweep)
    list(
        APPEND
        _engine_sim_offline_numeric_policy_targets
        engine_sim_offline_m4_bmw_torque_sweep
    )
endif()
foreach(_target IN LISTS _engine_sim_offline_numeric_policy_targets)
    engine_sim_offline_assert_renderer_numeric_policy("${_target}")
endforeach()
if(TARGET engine_sim_offline_m4_bmw_listening)
    engine_sim_offline_assert_final_renderer_numeric_policy(
        engine_sim_offline_m4_bmw_listening
    )
endif()
if(TARGET engine_sim_offline_m4_bmw_torque_sweep)
    engine_sim_offline_assert_final_renderer_numeric_policy(
        engine_sim_offline_m4_bmw_torque_sweep
    )
endif()
unset(_engine_sim_offline_numeric_policy_targets)

if(ENGINE_SIM_OFFLINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
