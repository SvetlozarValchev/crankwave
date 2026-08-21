cmake_minimum_required(VERSION 3.21)

project(
    engine_sim_offline
    VERSION 1.2.0
    DESCRIPTION "Offline-native engine simulation and audio rendering"
    LANGUAGES C CXX
)

set(
    ENGINE_SIM_OFFLINE_RELEASE_IDENTITY
    "${PROJECT_VERSION}"
    CACHE STRING
    "Canonical semantic release identity embedded in every distribution surface"
)
if(NOT ENGINE_SIM_OFFLINE_RELEASE_IDENTITY MATCHES
   "^[0-9]+\\.[0-9]+\\.[0-9]+([-+][0-9A-Za-z][0-9A-Za-z.+-]*)?$")
    message(FATAL_ERROR
        "ENGINE_SIM_OFFLINE_RELEASE_IDENTITY must be a portable semantic version")
endif()
if(NOT ENGINE_SIM_OFFLINE_RELEASE_IDENTITY STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "ENGINE_SIM_OFFLINE_RELEASE_IDENTITY must equal the product version ${PROJECT_VERSION}")
endif()

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
        src/authoring/parse_atlas_bake.cpp
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
        src/contract/audio_atlas.cpp
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
        src/artifacts/audio_atlas_directory_publisher.cpp
        src/artifacts/audio_atlas_manifest_encoder.cpp
        src/artifacts/directory_render_sink.cpp
        src/artifacts/directory_render_sink_commit.cpp
        src/artifacts/directory_render_sink_lifecycle.cpp
        src/artifacts/engine_telemetry_ndjson_encoder.cpp
        src/artifacts/audition_wav_encoder.cpp
        src/artifacts/manifest_encoder.cpp
        src/artifacts/manifest_encoder_output.cpp
        src/artifacts/manifest_encoder_presentation.cpp
        src/artifacts/vehicleengine_container.cpp
        src/artifacts/vehicleengine_package.cpp
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
        engine_sim_offline::authoring
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
        src/presentation/master_dynamics.cpp
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
        src/excitation/captured_source_excitation.cpp
        src/excitation/captured_source_excitation_compile.cpp
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
        src/session/exact_cycle_evidence.cpp
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

# The canonical diagnostic telemetry encoder consumes the public, callback-scoped
# EngineSessionBlockView. Keep that dependency explicit for static-library users.
target_link_libraries(
    engine_sim_offline_artifacts
    PRIVATE engine_sim_offline::session
)

add_library(
    engine_sim_offline_c_api
    STATIC
        src/c_api/compile.cpp
        src/c_api/context.cpp
        src/c_api/provenance.cpp
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

# Native responsive-audio cooking. This library drives the same authoritative
# C++ session used by finite audition renders; JavaScript/WASM is a playback and
# migration-oracle surface, not a production bake dependency.
add_library(
    engine_sim_offline_responsive
    STATIC
        src/responsive/directional_texture.cpp
        src/responsive/directional_planning.cpp
        src/responsive/directional_cook.cpp
        src/responsive/finite_capture.cpp
        src/responsive/held_alignment.cpp
        src/responsive/held_planning.cpp
        src/responsive/held_texture.cpp
        src/responsive/lifecycle_cook.cpp
        src/responsive/lifecycle_planning.cpp
        src/responsive/native_package.cpp
        src/responsive/native_publication.cpp
        src/responsive/package_child_encoders.cpp
        src/responsive/package_children.cpp
        src/responsive/presentation_transfer.cpp
        src/responsive/profile.cpp
        src/responsive/scenario_template.cpp
)
add_library(
    engine_sim_offline::responsive
    ALIAS engine_sim_offline_responsive
)

target_include_directories(
    engine_sim_offline_responsive
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    engine_sim_offline_responsive
    PUBLIC
        engine_sim_offline::authoring
        engine_sim_offline::compile
        engine_sim_offline::contract
        engine_sim_offline::session
        engine_sim_offline::build_options
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline::presentation
)
engine_sim_offline_enable_renderer_numeric_policy(engine_sim_offline_responsive)

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
            "SHELL:-sINCOMING_MODULE_JS_API=['locateFile','wasmBinary']"
            "SHELL:-sFILESYSTEM=0"
            "SHELL:-sALLOW_MEMORY_GROWTH=0"
            "SHELL:-sINITIAL_MEMORY=134217728"
            "SHELL:-sSTACK_SIZE=1048576"
            "SHELL:-sABORTING_MALLOC=0"
            "SHELL:-sEXIT_RUNTIME=0"
            "SHELL:-sSTRICT=1"
            "SHELL:-sWASM_BIGINT=1"
            "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAPU32','HEAPF32']"
            "SHELL:-sEXPORTED_FUNCTIONS=['_malloc','_free','_eso_api_version','_eso_get_abi_layout','_eso_context_create','_eso_context_destroy','_eso_context_get_last_error','_eso_context_copy_last_error_text','_eso_context_get_diagnostic','_eso_context_copy_diagnostic_text','_eso_context_get_related_diagnostic','_eso_context_copy_related_diagnostic_text','_eso_compile_engine_json','_eso_destroy_engine','_eso_engine_copy_id','_eso_engine_copy_provenance_sha256','_eso_renderer_copy_source_closure_sha256','_eso_compile_scenario_json','_eso_destroy_scenario','_eso_scenario_copy_id','_eso_create_session','_eso_destroy_session','_eso_session_get_descriptor','_eso_session_copy_identity','_eso_session_get_audio_bus_descriptor','_eso_session_copy_audio_bus_id','_eso_session_get_forward_gear_descriptor','_eso_session_copy_forward_gear_semantic_id','_eso_session_enqueue_controls','_eso_session_process']"
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
    engine_sim_offline_c_api
    PRIVATE engine_sim_offline_renderer_determinism
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
    include(GNUInstallDirs)
    find_package(Git QUIET)
    find_program(
        _engine_sim_offline_distribution_tar_executable
        NAMES tar
    )
    set(
        ENGINE_SIM_OFFLINE_DISTRIBUTION_OUTPUT_DIRECTORY
        "${PROJECT_BINARY_DIR}/distribution"
        CACHE PATH
        "Directory receiving the immutable installed release archive"
    )
    cmake_path(
        ABSOLUTE_PATH ENGINE_SIM_OFFLINE_DISTRIBUTION_OUTPUT_DIRECTORY
        BASE_DIRECTORY "${PROJECT_BINARY_DIR}"
        NORMALIZE
        OUTPUT_VARIABLE _engine_sim_offline_distribution_output_directory
    )

    set(
        _engine_sim_offline_install_resource_directory
        "${CMAKE_INSTALL_DATADIR}/engine-sim-offline/${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}"
    )
    file(
        RELATIVE_PATH
        ENGINE_SIM_OFFLINE_INSTALL_RESOURCE_RELATIVE_FROM_BINDIR
        "${CMAKE_INSTALL_FULL_BINDIR}"
        "${CMAKE_INSTALL_FULL_DATADIR}/engine-sim-offline/${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}"
    )
    file(
        RELATIVE_PATH
        ENGINE_SIM_OFFLINE_INSTALL_ASSET_RELATIVE_FROM_BINDIR
        "${CMAKE_INSTALL_FULL_BINDIR}"
        "${CMAKE_INSTALL_FULL_DATADIR}/engine-sim-offline/${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}/assets"
    )
    file(
        TO_CMAKE_PATH
        "${ENGINE_SIM_OFFLINE_INSTALL_RESOURCE_RELATIVE_FROM_BINDIR}"
        ENGINE_SIM_OFFLINE_INSTALL_RESOURCE_RELATIVE_FROM_BINDIR
    )
    file(
        TO_CMAKE_PATH
        "${ENGINE_SIM_OFFLINE_INSTALL_ASSET_RELATIVE_FROM_BINDIR}"
        ENGINE_SIM_OFFLINE_INSTALL_ASSET_RELATIVE_FROM_BINDIR
    )
    set(
        _engine_sim_offline_installed_generated_directory
        "${PROJECT_BINARY_DIR}/generated/installed-resources"
    )
    set(
        _engine_sim_offline_release_source_inputs_file
        "${_engine_sim_offline_installed_generated_directory}/release-source-inputs.txt"
    )
    file(MAKE_DIRECTORY "${_engine_sim_offline_installed_generated_directory}")
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/EngineSimOfflineInstalledLayout.hpp.in"
        "${_engine_sim_offline_installed_generated_directory}/engine_sim_offline_installed_layout.hpp"
        @ONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/EngineSimOfflineInstalledResources.package.json.in"
        "${_engine_sim_offline_installed_generated_directory}/package.json"
        @ONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/EngineSimOfflineInstalledRelease.js.in"
        "${_engine_sim_offline_installed_generated_directory}/release.js"
        @ONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/EngineSimOfflineVehicleEngineBakeWorkflowV2.json.in"
        "${_engine_sim_offline_installed_generated_directory}/vehicleengine-bake-workflow.v2.json"
        @ONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/GenerateInstalledDistributionManifest.cmake.in"
        "${_engine_sim_offline_installed_generated_directory}/generate-installed-distribution-manifest.cmake"
        @ONLY
    )

    set(
        _engine_sim_offline_builtin_asset_catalog_source
        "${PROJECT_SOURCE_DIR}/assets/builtin/catalog.v1.json"
    )
    set(
        _engine_sim_offline_ir_authoring_catalog_source
        "${PROJECT_SOURCE_DIR}/assets/builtin/ir-authoring-catalog.v1.json"
    )
    set(
        _engine_sim_offline_ir_library_payload_directory
        "${PROJECT_SOURCE_DIR}/assets/builtin/ir-library/payloads"
    )
    file(
        GLOB _engine_sim_offline_ir_library_payload_sources
        CONFIGURE_DEPENDS
        "${_engine_sim_offline_ir_library_payload_directory}/*.wav"
    )
    list(SORT _engine_sim_offline_ir_library_payload_sources)
    set(
        _engine_sim_offline_builtin_asset_bundle
        "${PROJECT_BINARY_DIR}/generated/engine-sim-offline-assets"
    )
    set(
        _engine_sim_offline_shared_starter_source
        "${PROJECT_SOURCE_DIR}/reference/fixtures/responsive-audio/shared-recorded-starter"
    )
    set(
        _engine_sim_offline_shared_starter_bundle
        "${_engine_sim_offline_builtin_asset_bundle}/runtime-audio/shared-recorded-starter"
    )
    set(
        _engine_sim_offline_shared_starter_runtime
        "${_engine_sim_offline_shared_starter_source}/runtime.json"
    )
    set(
        _engine_sim_offline_shared_starter_audio
        "${_engine_sim_offline_shared_starter_source}/audio/recorded-starter.cropped.192000hz.mono.f32le"
    )
    set(
        _engine_sim_offline_builtin_asset_payload_specs
        "f2875947eba2ed98a15f45a5d62f6ae7b607b0a56389bad5882d75748ad374b9|reference/fixtures/engine-sim-ir-library/presentation/mild_exhaust.wav"
        "0d736bb7065686ae527c8487ef7850762c0381c91125e555228cf4599e4ff68c|reference/fixtures/engine-sim-ir-library/presentation/minimal_muffling_01.wav"
        "55125069f43e48b52a5842649d899d9d250fc94808b8dbe59360e790aa0bebd4|data/profiles/raspy-muscle-620-cleanroom/accessory-configurations/raspy-muscle-620-cleanroom-warm-generic-accessories-v1.json"
        "58feca2e7b011a72a910be8a76dde71bdf53d3beedce5cdd54dbee583135f224|data/profiles/sequoia-3ur-fe-cleanroom/accessory-configurations/sequoia-3ur-fe-cleanroom-warm-generic-accessories-v1.json"
        "654ea801670c9e850a0406a0fb7afbc2b507fb6fd1cd70a2d8fbbfbab6dc6d00|data/engines/kohler-ch750-cleanroom/accessory-configuration.json"
        "915fafba878580a6ba9a316f65b552ac1da031ef7c7054267107d642ea0e6e79|data/engines/radial-5-cleanroom/accessory-configuration.json"
        "a1bcdbf0edd62a92ceddecd537f475b17cf2c6503fd65c3d2cf1b5d0d6315ede|data/profiles/bmw-m52tub28-cleanroom/accessory-configurations/bmw-m52tub28-cleanroom-warm-generic-accessories-v1.json"
        "ba86093015b191eea91f93549bd538466c3c62470e3f8a1e82d03156ab75c2d4|data/engines/shovelhead-bank-local-heads/accessory-configuration.json"
        "ca8dcf90b501d0f621efdd0137412af629b9710eca41cfb9de63667d4a98adcb|data/profiles/harley-evolution-1340-cleanroom/accessory-configurations/harley-evolution-1340-cleanroom-warm-generic-accessories-v1.json"
        "e90f6127213f989e6d75a99eab5bfc15e02d730be7fec7a219007ddd19033f4e|data/engines/subaru-ej25-cleanroom/accessory-configuration.json"
        "f6a52933a69429ee00e48ff47de613c9effb3509215219ab7b1bdae5e8be9c9b|data/profiles/honda-b18c5-cleanroom/accessory-configurations/honda-b18c5-cleanroom-warm-generic-accessories-v1.json"
        "ce3cd1bfa0265e5d82e93a70f515cd86d16efa8da4ad5432057372da2b9d8e97|data/profiles/bmw-m52b28/accessory-configurations/bmw-m52b28-warm-stock-accessories-v1.json"
    )
    set(_engine_sim_offline_builtin_asset_payload_commands)
    set(_engine_sim_offline_builtin_asset_payload_sources)
    foreach(
        _engine_sim_offline_builtin_asset_payload_spec
        IN LISTS _engine_sim_offline_builtin_asset_payload_specs
    )
        string(
            REPLACE "|" ";"
            _engine_sim_offline_builtin_asset_payload_parts
            "${_engine_sim_offline_builtin_asset_payload_spec}"
        )
        list(
            GET _engine_sim_offline_builtin_asset_payload_parts 0
            _engine_sim_offline_builtin_asset_payload_sha256
        )
        list(
            GET _engine_sim_offline_builtin_asset_payload_parts 1
            _engine_sim_offline_builtin_asset_payload_relative_path
        )
        set(
            _engine_sim_offline_builtin_asset_payload_source
            "${PROJECT_SOURCE_DIR}/${_engine_sim_offline_builtin_asset_payload_relative_path}"
        )
        list(
            APPEND _engine_sim_offline_builtin_asset_payload_sources
            "${_engine_sim_offline_builtin_asset_payload_source}"
        )
        list(
            APPEND _engine_sim_offline_builtin_asset_payload_commands
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${_engine_sim_offline_builtin_asset_payload_source}"
                "${_engine_sim_offline_builtin_asset_bundle}/payloads/${_engine_sim_offline_builtin_asset_payload_sha256}"
        )
    endforeach()
    foreach(
        _engine_sim_offline_ir_library_payload_source
        IN LISTS _engine_sim_offline_ir_library_payload_sources
    )
        get_filename_component(
            _engine_sim_offline_ir_library_payload_name
            "${_engine_sim_offline_ir_library_payload_source}"
            NAME_WE
        )
        string(LENGTH "${_engine_sim_offline_ir_library_payload_name}"
            _engine_sim_offline_ir_library_payload_name_length)
        if(NOT _engine_sim_offline_ir_library_payload_name_length EQUAL 64 OR
           NOT _engine_sim_offline_ir_library_payload_name MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR
                "IR library payload filename must be <sha256>.wav: ${_engine_sim_offline_ir_library_payload_source}")
        endif()
        list(
            APPEND _engine_sim_offline_builtin_asset_payload_commands
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${_engine_sim_offline_ir_library_payload_source}"
                "${_engine_sim_offline_builtin_asset_bundle}/payloads/${_engine_sim_offline_ir_library_payload_name}"
        )
    endforeach()
    set(
        _engine_sim_offline_builtin_asset_bundle_stamp
        "${_engine_sim_offline_builtin_asset_bundle}/.assembled"
    )
    add_custom_command(
        OUTPUT "${_engine_sim_offline_builtin_asset_bundle_stamp}"
        COMMAND
            "${CMAKE_COMMAND}" -E remove_directory
            "${_engine_sim_offline_builtin_asset_bundle}"
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${_engine_sim_offline_builtin_asset_bundle}/payloads"
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${_engine_sim_offline_shared_starter_bundle}/audio"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${_engine_sim_offline_builtin_asset_catalog_source}"
            "${_engine_sim_offline_builtin_asset_bundle}/catalog.v1.json"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${_engine_sim_offline_ir_authoring_catalog_source}"
            "${_engine_sim_offline_builtin_asset_bundle}/ir-authoring-catalog.v1.json"
        ${_engine_sim_offline_builtin_asset_payload_commands}
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${_engine_sim_offline_shared_starter_runtime}"
            "${_engine_sim_offline_shared_starter_bundle}/runtime.json"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${_engine_sim_offline_shared_starter_audio}"
            "${_engine_sim_offline_shared_starter_bundle}/audio/recorded-starter.cropped.192000hz.mono.f32le"
        COMMAND
            "${CMAKE_COMMAND}" -E touch
            "${_engine_sim_offline_builtin_asset_bundle_stamp}"
        DEPENDS
            "${_engine_sim_offline_builtin_asset_catalog_source}"
            "${_engine_sim_offline_ir_authoring_catalog_source}"
            ${_engine_sim_offline_builtin_asset_payload_sources}
            ${_engine_sim_offline_ir_library_payload_sources}
            "${_engine_sim_offline_shared_starter_runtime}"
            "${_engine_sim_offline_shared_starter_audio}"
        COMMENT "Assembling the built-in engine asset bundle"
        VERBATIM
    )
    add_custom_target(
        engine_sim_offline_builtin_assets
        DEPENDS "${_engine_sim_offline_builtin_asset_bundle_stamp}"
    )
    set(
        ENGINE_SIM_OFFLINE_BUILTIN_ASSET_CATALOG_FOR_TESTS
        "${_engine_sim_offline_builtin_asset_bundle}/catalog.v1.json"
    )
    set(
        ENGINE_SIM_OFFLINE_IR_AUTHORING_CATALOG_FOR_TESTS
        "${_engine_sim_offline_builtin_asset_bundle}/ir-authoring-catalog.v1.json"
    )

    add_library(
        engine_sim_offline_cli_support
        STATIC
            src/cli/bake_vehicleengine_command.cpp
            src/cli/cli_app.cpp
            src/cli/native_asset_root.cpp
            src/cli/native_builtin_asset_catalog.cpp
            src/cli/native_engine_input.cpp
            src/cli/native_input_files.cpp
            src/cli/native_input_filesystem.cpp
            src/cli/native_output_directory.cpp
            src/cli/native_responsive_bake_identity.cpp
            src/cli/vehicleengine_cli_support.cpp
    )

    target_include_directories(
        engine_sim_offline_cli_support
        PRIVATE
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/cli>
            $<BUILD_INTERFACE:${_engine_sim_offline_installed_generated_directory}>
    )

    target_compile_definitions(
        engine_sim_offline_cli_support
        PRIVATE
            ENGINE_SIM_OFFLINE_VERSION_LABEL="${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}"
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
            engine_sim_offline::responsive
            engine_sim_offline_renderer_determinism
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
    add_dependencies(
        engine_sim_offline_cli
        engine_sim_offline_builtin_assets
    )
    set_property(
        TARGET engine_sim_offline_cli
        APPEND PROPERTY LINK_DEPENDS
        "${_engine_sim_offline_builtin_asset_bundle_stamp}"
    )
    add_custom_command(
        TARGET engine_sim_offline_cli
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E remove_directory
            "$<TARGET_FILE_DIR:engine_sim_offline_cli>/engine-sim-offline-assets"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory
            "${_engine_sim_offline_builtin_asset_bundle}"
            "$<TARGET_FILE_DIR:engine_sim_offline_cli>/engine-sim-offline-assets"
        COMMENT "Bundling the built-in engine asset bundle beside the CLI"
        VERBATIM
    )

    install(
        TARGETS engine_sim_offline_cli
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(
        FILES
            "${_engine_sim_offline_installed_generated_directory}/package.json"
        DESTINATION "${_engine_sim_offline_install_resource_directory}"
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION "${_engine_sim_offline_install_resource_directory}/licenses"
        RENAME ENGINE-SIM-OFFLINE.txt
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${_engine_sim_offline_install_resource_directory}/licenses"
        RENAME THIRD-PARTY-NOTICES.md
    )
    install(
        FILES
            "${_engine_sim_offline_installed_generated_directory}/release.js"
        DESTINATION
            "${_engine_sim_offline_install_resource_directory}/web/runtime"
    )
    install(
        FILES
            "${_engine_sim_offline_installed_generated_directory}/vehicleengine-bake-workflow.v2.json"
        DESTINATION
            "${_engine_sim_offline_install_resource_directory}/contracts"
    )
    set(
        _engine_sim_offline_installed_contract_sources
        "${PROJECT_SOURCE_DIR}/docs/contracts/CLI_RESULT_V1.md"
        "${PROJECT_SOURCE_DIR}/docs/contracts/INSTALLED_DISTRIBUTION_V2.md"
        "${PROJECT_SOURCE_DIR}/docs/contracts/RESPONSIVE_PROFILE_SELECTION_V2.md"
        "${PROJECT_SOURCE_DIR}/docs/contracts/IR_AUTHORING_CATALOG_V1.md"
    )
    install(
        FILES ${_engine_sim_offline_installed_contract_sources}
        DESTINATION
            "${_engine_sim_offline_install_resource_directory}/docs/contracts"
    )
    file(
        GLOB _engine_sim_offline_responsive_profile_sources
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/tools/responsive-audio-baker/profiles/*.json"
    )
    install(
        FILES ${_engine_sim_offline_responsive_profile_sources}
        DESTINATION "${_engine_sim_offline_install_resource_directory}/profiles"
    )
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/schemas/responsive-audio-bake-profile.schema.json"
            "${PROJECT_SOURCE_DIR}/schemas/ir-authoring-catalog.schema.json"
            "${PROJECT_SOURCE_DIR}/schemas/installed-distribution.v2.schema.json"
            "${PROJECT_SOURCE_DIR}/schemas/vehicleengine-bake-workflow.v2.schema.json"
        DESTINATION
            "${_engine_sim_offline_install_resource_directory}/schemas"
    )
    set(
        _engine_sim_offline_vehicleengine_javascript_closure
        "${PROJECT_SOURCE_DIR}/web/runtime/directional-phase-cell.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/dry-directional-phase-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/held-phase-texture-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/held-texture-presentation-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/renderer-runtime-compatibility.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/responsive-audio-lifecycle-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/vehicleengine-audio-engine.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/vehicleengine-package.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/shared-recorded-starter-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/state-phase-texture-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/steady-transient-envelope.js"
    )
    install(
        FILES ${_engine_sim_offline_vehicleengine_javascript_closure}
        DESTINATION
            "${_engine_sim_offline_install_resource_directory}/web/runtime"
    )
    set(
        _engine_sim_offline_release_source_inputs
        "${_engine_sim_offline_builtin_asset_catalog_source}"
        "${_engine_sim_offline_ir_authoring_catalog_source}"
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        ${_engine_sim_offline_builtin_asset_payload_sources}
        ${_engine_sim_offline_ir_library_payload_sources}
        "${_engine_sim_offline_shared_starter_runtime}"
        "${_engine_sim_offline_shared_starter_audio}"
        ${_engine_sim_offline_responsive_profile_sources}
        "${PROJECT_SOURCE_DIR}/schemas/responsive-audio-bake-profile.schema.json"
        "${PROJECT_SOURCE_DIR}/schemas/ir-authoring-catalog.schema.json"
        "${PROJECT_SOURCE_DIR}/schemas/installed-distribution.v2.schema.json"
        "${PROJECT_SOURCE_DIR}/schemas/vehicleengine-bake-workflow.v2.schema.json"
        ${_engine_sim_offline_vehicleengine_javascript_closure}
        ${_engine_sim_offline_installed_contract_sources}
        "${PROJECT_SOURCE_DIR}/cmake/EngineSimOfflineInstalledResources.package.json.in"
        "${PROJECT_SOURCE_DIR}/cmake/EngineSimOfflineInstalledRelease.js.in"
        "${PROJECT_SOURCE_DIR}/cmake/EngineSimOfflineVehicleEngineBakeWorkflowV2.json.in"
        "${PROJECT_SOURCE_DIR}/cmake/GenerateInstalledDistributionManifest.cmake.in"
    )
    list(REMOVE_DUPLICATES _engine_sim_offline_release_source_inputs)
    list(SORT _engine_sim_offline_release_source_inputs)
    set(_engine_sim_offline_release_source_input_text "")
    foreach(
        _engine_sim_offline_release_source_input
        IN LISTS _engine_sim_offline_release_source_inputs
    )
        file(
            RELATIVE_PATH _engine_sim_offline_release_source_input_relative
            "${PROJECT_SOURCE_DIR}"
            "${_engine_sim_offline_release_source_input}"
        )
        file(
            TO_CMAKE_PATH
            "${_engine_sim_offline_release_source_input_relative}"
            _engine_sim_offline_release_source_input_relative
        )
        if(NOT _engine_sim_offline_release_source_input_relative MATCHES
           "^[A-Za-z0-9._+/@-]+$" OR
           _engine_sim_offline_release_source_input_relative MATCHES
           "(^|/)\\.\\.(/|$)")
            message(FATAL_ERROR
                "installed release source input is not portable: ${_engine_sim_offline_release_source_input_relative}")
        endif()
        string(APPEND _engine_sim_offline_release_source_input_text
            "${_engine_sim_offline_release_source_input_relative}\n")
    endforeach()
    file(WRITE "${_engine_sim_offline_release_source_inputs_file}"
        "${_engine_sim_offline_release_source_input_text}")
    install(
        FILES "${_engine_sim_offline_builtin_asset_catalog_source}"
        DESTINATION "${_engine_sim_offline_install_resource_directory}/assets"
        RENAME catalog.v1.json
    )
    install(
        FILES "${_engine_sim_offline_ir_authoring_catalog_source}"
        DESTINATION "${_engine_sim_offline_install_resource_directory}/assets"
        RENAME ir-authoring-catalog.v1.json
    )
    install(
        FILES "${_engine_sim_offline_shared_starter_runtime}"
        DESTINATION
            "${_engine_sim_offline_install_resource_directory}/assets/runtime-audio/shared-recorded-starter"
    )
    install(
        FILES "${_engine_sim_offline_shared_starter_audio}"
        DESTINATION
            "${_engine_sim_offline_install_resource_directory}/assets/runtime-audio/shared-recorded-starter/audio"
    )
    foreach(
        _engine_sim_offline_builtin_asset_payload_spec
        IN LISTS _engine_sim_offline_builtin_asset_payload_specs
    )
        string(
            REPLACE "|" ";"
            _engine_sim_offline_builtin_asset_payload_parts
            "${_engine_sim_offline_builtin_asset_payload_spec}"
        )
        list(
            GET _engine_sim_offline_builtin_asset_payload_parts 0
            _engine_sim_offline_builtin_asset_payload_sha256
        )
        list(
            GET _engine_sim_offline_builtin_asset_payload_parts 1
            _engine_sim_offline_builtin_asset_payload_relative_path
        )
        install(
            FILES
                "${PROJECT_SOURCE_DIR}/${_engine_sim_offline_builtin_asset_payload_relative_path}"
            DESTINATION
                "${_engine_sim_offline_install_resource_directory}/assets/payloads"
            RENAME "${_engine_sim_offline_builtin_asset_payload_sha256}"
        )
    endforeach()
    foreach(
        _engine_sim_offline_ir_library_payload_source
        IN LISTS _engine_sim_offline_ir_library_payload_sources
    )
        get_filename_component(
            _engine_sim_offline_ir_library_payload_name
            "${_engine_sim_offline_ir_library_payload_source}"
            NAME_WE
        )
        install(
            FILES "${_engine_sim_offline_ir_library_payload_source}"
            DESTINATION
                "${_engine_sim_offline_install_resource_directory}/assets/payloads"
            RENAME "${_engine_sim_offline_ir_library_payload_name}"
        )
    endforeach()

    install(
        SCRIPT
            "${_engine_sim_offline_installed_generated_directory}/generate-installed-distribution-manifest.cmake"
    )

    add_custom_target(
        engine_sim_offline_distribution
        COMMAND
            "${CMAKE_COMMAND}" -E remove_directory
            "${_engine_sim_offline_distribution_output_directory}/$<CONFIG>/stage"
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f
            "${_engine_sim_offline_distribution_output_directory}/$<CONFIG>/engine-sim-offline-${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}.tar"
            "${_engine_sim_offline_distribution_output_directory}/$<CONFIG>/engine-sim-offline-${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}.tar.sha256"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE_STAMP=${PROJECT_BINARY_DIR}/generated/engine_sim_offline_generated/renderer_source_stamp_generated.hpp"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
            "-DRELEASE_SOURCE_INPUTS=${_engine_sim_offline_release_source_inputs_file}"
            "-DRELEASE_SOURCE_IDENTITY_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/GenerateInstalledDistributionSourceIdentity.cmake"
            -P "${PROJECT_SOURCE_DIR}/cmake/RequireCompleteDistribution.cmake"
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${_engine_sim_offline_distribution_output_directory}/$<CONFIG>/stage"
        COMMAND
            "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}"
            --config "$<CONFIG>"
            --prefix
            "${_engine_sim_offline_distribution_output_directory}/$<CONFIG>/stage/engine-sim-offline-${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPREFIX_DIRECTORY=${_engine_sim_offline_distribution_output_directory}/$<CONFIG>/stage/engine-sim-offline-${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}"
            "-DRESOURCE_RELATIVE_DIRECTORY=${_engine_sim_offline_install_resource_directory}"
            "-DRELEASE_IDENTITY=${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}"
            "-DOUTPUT_ARCHIVE=${_engine_sim_offline_distribution_output_directory}/$<CONFIG>/engine-sim-offline-${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}.tar"
            "-DTAR_EXECUTABLE=${_engine_sim_offline_distribution_tar_executable}"
            -DREQUIRE_COMPLETE=TRUE
            -P
            "${PROJECT_SOURCE_DIR}/cmake/CreateDeterministicDistributionArchive.cmake"
        DEPENDS
            engine_sim_offline_builtin_assets
            engine_sim_offline_cli
        BYPRODUCTS
            "${_engine_sim_offline_distribution_output_directory}/$<CONFIG>/engine-sim-offline-${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}.tar"
            "${_engine_sim_offline_distribution_output_directory}/$<CONFIG>/engine-sim-offline-${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}.tar.sha256"
        COMMENT "Assembling the complete immutable engine-sim-offline distribution"
        VERBATIM
    )
    set(
        ENGINE_SIM_OFFLINE_INSTALL_BINDIR_FOR_TESTS
        "${CMAKE_INSTALL_BINDIR}"
    )
    set(
        ENGINE_SIM_OFFLINE_INSTALL_DATADIR_FOR_TESTS
        "${CMAKE_INSTALL_DATADIR}"
    )
    set(
        ENGINE_SIM_OFFLINE_RELEASE_IDENTITY_FOR_TESTS
        "${ENGINE_SIM_OFFLINE_RELEASE_IDENTITY}"
    )
    set(
        ENGINE_SIM_OFFLINE_DISTRIBUTION_TAR_FOR_TESTS
        "${_engine_sim_offline_distribution_tar_executable}"
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
