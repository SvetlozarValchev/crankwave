cmake_minimum_required(VERSION 3.21)

project(
    engine_sim_offline
    DESCRIPTION "Offline-native engine simulation and audio rendering"
    LANGUAGES C CXX
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
    ENGINE_SIM_OFFLINE_BUILD_WASM
    "Build the fixed-memory browser WebAssembly module"
    "${EMSCRIPTEN}"
)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
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
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<PLATFORM_ID:Emscripten>>:-fwasm-exceptions;-fno-lto>
)

include(cmake/RendererNumericPolicy.cmake)
engine_sim_offline_define_renderer_numeric_policy()

add_library(
    engine_sim_offline_authoring
    STATIC
        src/authoring/document_reader.cpp
        src/authoring/json.cpp
        src/authoring/parse_engine.cpp
        src/authoring/parse_engine_common.cpp
        src/authoring/parse_engine_gas.cpp
        src/authoring/parse_engine_mechanism.cpp
        src/authoring/parse_engine_presentation.cpp
        src/authoring/parse_engine_references.cpp
        src/authoring/parse_engine_systems.cpp
        src/authoring/parse_engine_valvetrain.cpp
        src/authoring/parse_scenario.cpp
)
add_library(
    engine_sim_offline::authoring
    ALIAS engine_sim_offline_authoring
)

target_include_directories(
    engine_sim_offline_authoring
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_authoring
    PUBLIC engine_sim_offline::build_options
)

add_library(
    engine_sim_offline_contract
    STATIC
        src/contract/capture.cpp
        src/contract/common.cpp
        src/contract/engine.cpp
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
    engine_sim_offline_compile
    STATIC
        src/compile/compile_api.cpp
        src/compile/compiled_model_builder.cpp
        src/compile/compiled_models.cpp
        src/compile/engine_asset_resolver.cpp
        src/compile/engine_capability_admission.cpp
        src/compile/engine_combustion_resolver.cpp
        src/compile/engine_gas_resolver.cpp
        src/compile/engine_mechanism_resolver.cpp
        src/compile/engine_operating_admission.cpp
        src/compile/engine_physical_admission.cpp
        src/compile/engine_physics_resolver.cpp
        src/compile/engine_throttle_resolver.cpp
        src/compile/engine_presentation_admission.cpp
        src/compile/engine_presentation_resolver.cpp
        src/compile/engine_resolution_support.cpp
        src/compile/engine_resolver.cpp
        src/compile/engine_rig_resolver.cpp
        src/compile/engine_runtime_ids.cpp
        src/compile/engine_topology_resolver.cpp
        src/compile/resolution_builder.cpp
        src/compile/scenario_document_resolver.cpp
        src/compile/scenario_mode_resolver.cpp
        src/compile/scenario_output_resolver.cpp
        src/compile/scenario_provenance_resolver.cpp
        src/compile/scenario_resolver.cpp
        src/compile/scenario_resolver_support.cpp
        src/compile/scenario_trajectory_resolver.cpp
        src/compile/si_conversion.cpp
        src/compile/stable_id.cpp
)
add_library(
    engine_sim_offline::compile
    ALIAS engine_sim_offline_compile
)

target_include_directories(
    engine_sim_offline_compile
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_compile
    PUBLIC
        engine_sim_offline::authoring
        engine_sim_offline::contract
        engine_sim_offline::build_options
    PRIVATE
        engine_sim_offline::presentation
        engine_sim_offline::simulation
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_compile)

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
    engine_sim_offline_simulation
    STATIC
        src/simulation/bounded_clutch_coupling.cpp
        src/simulation/bounded_dyno_constraint.cpp
        src/simulation/centered_slider_crank_equivalent_inertia.cpp
        src/simulation/coupled_free_vehicle_drivetrain.cpp
        src/simulation/fixed_horizon_cycle_sampling.cpp
        src/simulation/free_engine_method_registry.cpp
        src/simulation/free_vehicle_method_registry.cpp
        src/simulation/chen_flynn_cycle_mean_loss.cpp
        src/simulation/chen_flynn_per_cylinder_travel_cycle_mean_loss.cpp
        src/simulation/cycle_accounting_method_descriptors.cpp
        src/simulation/cycle_accounting_method_registry.cpp
        src/simulation/engine_sim_v1_starter_motor.cpp
        src/simulation/engine_sim_v1_transient_friction.cpp
        src/simulation/four_stroke_cycle_integrator.cpp
        src/simulation/forward_vehicle_road_load.cpp
        src/simulation/legacy_combustion_primitives.cpp
        src/simulation/legacy_fixed_valvetrain.cpp
        src/simulation/legacy_flow_calibration.cpp
        src/simulation/legacy_gas_primitives.cpp
        src/simulation/legacy_ignition_schedule.cpp
        src/simulation/legacy_vtec_selector.cpp
        src/simulation/inertial_crank_dynamics.cpp
        src/simulation/positive_speed_rigid_crank_zoh.cpp
        src/simulation/rigid_crank_group.cpp
        src/simulation/inertial_dyno_method_registry.cpp
        src/simulation/legacy_low_order_gas.cpp
        src/simulation/legacy_low_order_gas_compile.cpp
        src/simulation/low_order_capture_buffer.cpp
        src/simulation/legacy_low_order_mechanics.cpp
        src/simulation/legacy_low_order_mechanics_compile.cpp
        src/simulation/mechanism_kinematics_plan.cpp
        src/simulation/one_level_master_rod_configuration_inertia.cpp
        src/simulation/one_level_master_rod_coupled_reaction.cpp
        src/simulation/one_level_master_rod_cycle_accounting_plan.cpp
        src/simulation/one_level_master_rod_cycle_mean_inertia.cpp
        src/simulation/one_level_master_rod_kinematics.cpp
        src/simulation/low_order_capture_session.cpp
        src/simulation/low_order_capture_session_compile.cpp
        src/simulation/legacy_mechanics_primitives.cpp
        src/simulation/kinematic_scenario_schedule.cpp
        src/simulation/low_order_capture_plan.cpp
        src/simulation/low_order_engine_core_v1_runtime.cpp
        src/simulation/low_order_dynamic_crank_runtime.cpp
        src/simulation/low_order_dynamic_crank_runtime_compile.cpp
        src/simulation/low_order_inertial_dyno_v1_runtime.cpp
        src/simulation/low_order_inertial_dyno_v1_runtime_compile.cpp
        src/simulation/low_order_operating_point_v1_runtime.cpp
        src/simulation/low_order_operating_point_v1_runtime_compile.cpp
        src/simulation/low_order_prescribed_kinematic_runtime.cpp
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

add_library(
    engine_sim_offline_bake
    STATIC
        src/bake.cpp
        src/render/native_bake_plan.cpp
        src/render/native_presentation_publisher.cpp
        src/render/render_job_derivation.cpp
        src/render/render_job_failure.cpp
        src/render/render_request.cpp
)
add_library(
    engine_sim_offline::bake
    ALIAS engine_sim_offline_bake
)

target_include_directories(
    engine_sim_offline_bake
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_bake
    PUBLIC
        engine_sim_offline::contract
        engine_sim_offline::build_options
    PRIVATE
        engine_sim_offline::request_identity
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_bake)

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
        src/presentation/presentation_audio_session.cpp
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
    engine_sim_offline_session
    STATIC
        src/session/control_timeline.cpp
        src/session/engine_session.cpp
        src/session/session_build.cpp
)
add_library(
    engine_sim_offline::session
    ALIAS engine_sim_offline_session
)

target_include_directories(
    engine_sim_offline_session
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_session
    PUBLIC
        engine_sim_offline::compile
        engine_sim_offline::contract
        engine_sim_offline::build_options
    PRIVATE
        engine_sim_offline::excitation
        engine_sim_offline::presentation
        engine_sim_offline::request_identity
        engine_sim_offline::simulation
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_session)

add_library(
    engine_sim_offline_c_api
    STATIC
        src/c_api/compile.cpp
        src/c_api/context.cpp
        src/c_api/session.cpp
        src/c_api/telemetry.cpp
)
add_library(
    engine_sim_offline::c_api
    ALIAS engine_sim_offline_c_api
)

target_include_directories(
    engine_sim_offline_c_api
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_c_api
    PRIVATE
        engine_sim_offline::authoring
        engine_sim_offline::build_options
        engine_sim_offline::compile
        engine_sim_offline::session
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_c_api)

if(EMSCRIPTEN OR ENGINE_SIM_OFFLINE_BUILD_WASM)
    include(cmake/EngineSimOfflineWasm.cmake)
endif()

if(ENGINE_SIM_OFFLINE_BUILD_WASM)
    add_executable(
        engine_sim_offline_wasm
        src/wasm/module.cpp
    )
    set_target_properties(
        engine_sim_offline_wasm
        PROPERTIES OUTPUT_NAME engine-sim-offline
    )

    target_link_libraries(
        engine_sim_offline_wasm
        PRIVATE engine_sim_offline::c_api
    )
    target_link_options(
        engine_sim_offline_wasm
        PRIVATE
            --no-entry
            -fwasm-exceptions
            "SHELL:-sMODULARIZE=1"
            "SHELL:-sEXPORT_ES6=1"
            "SHELL:-sENVIRONMENT=web,worker,node"
            "SHELL:-sFILESYSTEM=0"
            "SHELL:-sALLOW_MEMORY_GROWTH=0"
            "SHELL:-sINITIAL_MEMORY=134217728"
            "SHELL:-sSTACK_SIZE=1048576"
            "SHELL:-sABORTING_MALLOC=0"
            "SHELL:-sEXIT_RUNTIME=0"
            "SHELL:-sSTRICT=1"
            "SHELL:-sWASM_BIGINT=1"
            "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAPU32','HEAPF32']"
            "SHELL:-sEXPORTED_FUNCTIONS=['_malloc','_free','_eso_api_version','_eso_get_abi_layout','_eso_context_create','_eso_context_destroy','_eso_context_get_last_error','_eso_context_copy_last_error_text','_eso_context_get_diagnostic','_eso_context_copy_diagnostic_text','_eso_context_get_related_diagnostic','_eso_context_copy_related_diagnostic_text','_eso_compile_engine_json','_eso_destroy_engine','_eso_engine_copy_id','_eso_compile_scenario_json','_eso_destroy_scenario','_eso_scenario_copy_id','_eso_create_session','_eso_destroy_session','_eso_session_get_descriptor','_eso_session_copy_identity','_eso_session_get_audio_bus_descriptor','_eso_session_copy_audio_bus_id','_eso_session_get_forward_gear_descriptor','_eso_session_copy_forward_gear_semantic_id','_eso_session_enqueue_controls','_eso_session_process']"
    )
    engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_wasm)
    engine_sim_offline_configure_wasm_numeric_target(engine_sim_offline_wasm)

    include(cmake/EngineSimOfflineWorkbench.cmake)
    engine_sim_offline_add_web_workbench(engine_sim_offline_wasm)
endif()

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
    engine_sim_offline_bake
    PRIVATE
        engine_sim_offline::compile
        engine_sim_offline::simulation
        engine_sim_offline::excitation
        engine_sim_offline::presentation
        engine_sim_offline::session
        engine_sim_offline::artifacts
        engine_sim_offline_execution
        engine_sim_offline_renderer_determinism
)

if(ENGINE_SIM_OFFLINE_BUILD_TESTS)
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

endif()

if(ENGINE_SIM_OFFLINE_BUILD_CLI)
    add_library(
        engine_sim_offline_cli_support
        STATIC
            src/cli/cli_app.cpp
            src/cli/native_asset_root.cpp
            src/cli/native_engine_input.cpp
            src/cli/native_input_files.cpp
            src/cli/native_input_filesystem.cpp
            src/cli/native_output_directory.cpp
    )

    target_include_directories(
        engine_sim_offline_cli_support
        PRIVATE
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/cli>
    )

    target_compile_definitions(
        engine_sim_offline_cli_support
        PRIVATE ENGINE_SIM_OFFLINE_VERSION_LABEL="development"
    )

    target_link_libraries(
        engine_sim_offline_cli_support
        PUBLIC
            engine_sim_offline::build_options
        PRIVATE
            engine_sim_offline::artifacts
            engine_sim_offline::authoring
            engine_sim_offline::compile
            engine_sim_offline::bake
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_cli_support
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
        PRIVATE
            engine_sim_offline_cli_support
            $<$<PLATFORM_ID:Linux>:m>
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(
            engine_sim_offline_cli
            PRIVATE LINKER:--no-as-needed
        )
    endif()
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_cli
    )
    engine_sim_offline_finalize_renderer_numeric_policy(
        engine_sim_offline_cli
    )
endif()

set(
    _engine_sim_offline_numeric_policy_targets
    engine_sim_offline_contract
    engine_sim_offline_simulation
    engine_sim_offline_bake
    engine_sim_offline_artifacts
    engine_sim_offline_dsp
    engine_sim_offline_presentation
    engine_sim_offline_excitation
    engine_sim_offline_session
    engine_sim_offline_c_api
    engine_sim_offline_execution
    engine_sim_offline_renderer_determinism
)
if(TARGET engine_sim_offline_wasm)
    list(
        APPEND
        _engine_sim_offline_numeric_policy_targets
        engine_sim_offline_wasm
    )
endif()
if(ENGINE_SIM_OFFLINE_BUILD_TESTS)
    list(
        APPEND
        _engine_sim_offline_numeric_policy_targets
        engine_sim_offline_reference_parity_v1_reader
        engine_sim_offline_p18_reference_catalog
        engine_sim_offline_p18_reference_fixture
    )
endif()
if(TARGET engine_sim_offline_cli_support)
    list(
        APPEND
        _engine_sim_offline_numeric_policy_targets
        engine_sim_offline_cli_support
        engine_sim_offline_cli
    )
endif()
foreach(_target IN LISTS _engine_sim_offline_numeric_policy_targets)
    engine_sim_offline_assert_renderer_numeric_policy("${_target}")
endforeach()
if(TARGET engine_sim_offline_cli)
    engine_sim_offline_assert_final_renderer_numeric_policy(
        engine_sim_offline_cli
    )
endif()
unset(_engine_sim_offline_numeric_policy_targets)

if(ENGINE_SIM_OFFLINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
