cmake_minimum_required(VERSION 3.21)

project(
    crankwave
    VERSION 1.2.0
    DESCRIPTION "Deterministic engine simulation and responsive audio packaging"
    LANGUAGES C CXX
)

set(
    CRANKWAVE_RELEASE_IDENTITY
    "${PROJECT_VERSION}"
    CACHE STRING
    "Canonical semantic release identity embedded in every distribution surface"
)
if(NOT CRANKWAVE_RELEASE_IDENTITY MATCHES
   "^[0-9]+\\.[0-9]+\\.[0-9]+([-+][0-9A-Za-z][0-9A-Za-z.+-]*)?$")
    message(FATAL_ERROR
        "CRANKWAVE_RELEASE_IDENTITY must be a portable semantic version")
endif()
if(NOT CRANKWAVE_RELEASE_IDENTITY STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "CRANKWAVE_RELEASE_IDENTITY must equal the product version ${PROJECT_VERSION}")
endif()

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are unsupported; use cmake -S . -B build")
endif()

option(
    CRANKWAVE_BUILD_TESTS
    "Build the focused crankwave test targets"
    "${PROJECT_IS_TOP_LEVEL}"
)
option(
    CRANKWAVE_BUILD_CLI
    "Build the crankwave command-line shell"
    "${PROJECT_IS_TOP_LEVEL}"
)
option(
    CRANKWAVE_BUILD_WASM
    "Build the fixed-memory browser WebAssembly module"
    "${EMSCRIPTEN}"
)
if(CRANKWAVE_BUILD_TESTS OR CRANKWAVE_BUILD_CLI OR CRANKWAVE_BUILD_WASM)
    file(READ "${PROJECT_SOURCE_DIR}/source-assets.lock.json" _source_assets_lock)
    string(JSON _first_source_asset GET "${_source_assets_lock}" files 0 path)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${_first_source_asset}")
        message(FATAL_ERROR
            "Source assets are not installed. Run: node scripts/source-assets.mjs fetch")
    endif()
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(crankwave_build_options INTERFACE)
add_library(
    crankwave::build_options
    ALIAS crankwave_build_options
)

target_compile_features(crankwave_build_options INTERFACE cxx_std_20)

target_compile_options(
    crankwave_build_options
    INTERFACE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic;-fno-fast-math;-ffp-contract=off>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/fp:strict;/Zc:__cplusplus>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<PLATFORM_ID:Emscripten>>:-fwasm-exceptions;-fno-lto>
)

include(cmake/RendererNumericPolicy.cmake)
crankwave_define_renderer_numeric_policy()

add_library(
    crankwave_authoring
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
    crankwave::authoring
    ALIAS crankwave_authoring
)

target_include_directories(
    crankwave_authoring
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_authoring
    PUBLIC crankwave::build_options
)

add_library(
    crankwave_contract
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
    crankwave::contract
    ALIAS crankwave_contract
)

target_include_directories(
    crankwave_contract
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(
    crankwave_contract
    PUBLIC crankwave::build_options
)
crankwave_enable_renderer_numeric_policy(crankwave_contract)

add_library(
    crankwave_compile
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
    crankwave::compile
    ALIAS crankwave_compile
)

target_include_directories(
    crankwave_compile
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_compile
    PUBLIC
        crankwave::authoring
        crankwave::contract
        crankwave::build_options
    PRIVATE
        crankwave::presentation
        crankwave::simulation
)
crankwave_enable_renderer_numeric_policy(crankwave_compile)

add_library(
    crankwave_request_identity
    STATIC
        src/identity/canonical_json_writer.cpp
        src/identity/simulation_request_identity.cpp
        src/identity/simulation_request_identity_engine.cpp
        src/identity/simulation_request_identity_scenario.cpp
        src/identity/simulation_request_identity_writer.cpp
)
add_library(
    crankwave::request_identity
    ALIAS crankwave_request_identity
)

target_include_directories(
    crankwave_request_identity
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_request_identity
    PUBLIC
        crankwave::contract
        crankwave::build_options
)
crankwave_enable_renderer_numeric_policy(
    crankwave_request_identity
)

add_library(
    crankwave_simulation
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
        src/simulation/crankwave_starter_motor.cpp
        src/simulation/crankwave_transient_friction.cpp
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
    crankwave::simulation
    ALIAS crankwave_simulation
)

target_include_directories(
    crankwave_simulation
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_simulation
    PUBLIC
        crankwave::contract
        crankwave::build_options
    PRIVATE crankwave::dsp
)
crankwave_enable_renderer_numeric_policy(crankwave_simulation)

add_library(
    crankwave_bake
    STATIC
        src/bake.cpp
        src/render/native_bake_plan.cpp
        src/render/native_presentation_publisher.cpp
        src/render/render_job_derivation.cpp
        src/render/render_job_failure.cpp
        src/render/render_request.cpp
)
add_library(
    crankwave::bake
    ALIAS crankwave_bake
)

target_include_directories(
    crankwave_bake
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_bake
    PUBLIC
        crankwave::contract
        crankwave::build_options
    PRIVATE
        crankwave::request_identity
)
crankwave_enable_renderer_numeric_policy(crankwave_bake)

add_library(
    crankwave_artifacts
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
        src/artifacts/crankwave_container.cpp
        src/artifacts/crankwave_package.cpp
        src/artifacts/secure_filesystem_support.cpp
        src/artifacts/simulation_manifest_encoder.cpp
        src/artifacts/telemetry_encoder.cpp
        src/artifacts/telemetry_encoder_rows.cpp
        src/artifacts/telemetry_encoder_validation.cpp
        src/artifacts/wav_encoder.cpp
)
add_library(
    crankwave::artifacts
    ALIAS crankwave_artifacts
)

target_include_directories(
    crankwave_artifacts
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/artifacts>
)

target_link_libraries(
    crankwave_artifacts
    PUBLIC
        crankwave::contract
        crankwave::build_options
    PRIVATE
        crankwave::authoring
        crankwave::request_identity
)
crankwave_enable_renderer_numeric_policy(crankwave_artifacts)

add_library(
    crankwave_dsp
    STATIC
        src/dsp/causal_reconstruction_table.cpp
        src/dsp/fixed_fft.cpp
        src/dsp/pcg32.cpp
        src/dsp/source_conditioning_primitives.cpp
        src/dsp/static_ir_conversion.cpp
)
add_library(
    crankwave::dsp
    ALIAS crankwave_dsp
)

target_include_directories(
    crankwave_dsp
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_dsp
    PUBLIC crankwave::build_options
)
crankwave_enable_renderer_numeric_policy(crankwave_dsp)

add_library(
    crankwave_presentation
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
    crankwave::presentation
    ALIAS crankwave_presentation
)

target_include_directories(
    crankwave_presentation
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_presentation
    PUBLIC
        crankwave::build_options
        crankwave::contract
        crankwave::dsp
)
crankwave_enable_renderer_numeric_policy(crankwave_presentation)

add_library(
    crankwave_excitation
    STATIC
        src/excitation/captured_source_excitation.cpp
        src/excitation/captured_source_excitation_compile.cpp
)
add_library(
    crankwave::excitation
    ALIAS crankwave_excitation
)

target_include_directories(
    crankwave_excitation
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_excitation
    PUBLIC
        crankwave::contract
    PRIVATE crankwave::build_options
)
crankwave_enable_renderer_numeric_policy(crankwave_excitation)

add_library(
    crankwave_session
    STATIC
        src/session/control_timeline.cpp
        src/session/engine_session.cpp
        src/session/exact_cycle_evidence.cpp
        src/session/session_build.cpp
)
add_library(
    crankwave::session
    ALIAS crankwave_session
)

target_include_directories(
    crankwave_session
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_session
    PUBLIC
        crankwave::compile
        crankwave::contract
        crankwave::build_options
    PRIVATE
        crankwave::excitation
        crankwave::presentation
        crankwave::request_identity
        crankwave::simulation
)
crankwave_enable_renderer_numeric_policy(crankwave_session)

# The canonical diagnostic telemetry encoder consumes the public, callback-scoped
# EngineSessionBlockView. Keep that dependency explicit for static-library users.
target_link_libraries(
    crankwave_artifacts
    PRIVATE crankwave::session
)

add_library(
    crankwave_c_api
    STATIC
        src/c_api/compile.cpp
        src/c_api/context.cpp
        src/c_api/provenance.cpp
        src/c_api/session.cpp
        src/c_api/telemetry.cpp
)
add_library(
    crankwave::c_api
    ALIAS crankwave_c_api
)

target_include_directories(
    crankwave_c_api
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_c_api
    PRIVATE
        crankwave::authoring
        crankwave::build_options
        crankwave::compile
        crankwave::session
)
crankwave_enable_renderer_numeric_policy(crankwave_c_api)

# Native responsive-audio cooking. This library drives the same authoritative
# C++ session used by finite audition renders; JavaScript/WASM is a playback and
# migration-oracle surface, not a production bake dependency.
add_library(
    crankwave_responsive
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
    crankwave::responsive
    ALIAS crankwave_responsive
)

target_include_directories(
    crankwave_responsive
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_responsive
    PUBLIC
        crankwave::authoring
        crankwave::compile
        crankwave::contract
        crankwave::session
        crankwave::build_options
    PRIVATE
        crankwave::artifacts
        crankwave::presentation
)
crankwave_enable_renderer_numeric_policy(crankwave_responsive)

# The full responsive baker is isolated from the realtime C API so browser
# distributions can keep the live module fixed-memory while loading this much
# heavier surface only inside a dedicated offline worker.
add_library(
    crankwave_bake_c_api
    STATIC
        src/c_api/bake.cpp
)
add_library(
    crankwave::bake_c_api
    ALIAS crankwave_bake_c_api
)
target_include_directories(
    crankwave_bake_c_api
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)
target_link_libraries(
    crankwave_bake_c_api
    PRIVATE
        crankwave::c_api
        crankwave::responsive
        crankwave_renderer_determinism
)
crankwave_enable_renderer_numeric_policy(crankwave_bake_c_api)

if(EMSCRIPTEN OR CRANKWAVE_BUILD_WASM)
    include(cmake/CrankwaveWasm.cmake)
endif()

if(CRANKWAVE_BUILD_WASM)
    add_executable(
        crankwave_wasm
        src/wasm/module.cpp
    )
    set_target_properties(
        crankwave_wasm
        PROPERTIES OUTPUT_NAME crankwave
    )

    target_link_libraries(
        crankwave_wasm
        PRIVATE crankwave::c_api
    )
    target_link_options(
        crankwave_wasm
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
            "SHELL:-sEXPORTED_FUNCTIONS=['_malloc','_free','_crankwave_api_version','_crankwave_get_abi_layout','_crankwave_context_create','_crankwave_context_destroy','_crankwave_context_get_last_error','_crankwave_context_copy_last_error_text','_crankwave_context_get_diagnostic','_crankwave_context_copy_diagnostic_text','_crankwave_context_get_related_diagnostic','_crankwave_context_copy_related_diagnostic_text','_crankwave_compile_engine_json','_crankwave_destroy_engine','_crankwave_engine_copy_id','_crankwave_engine_copy_provenance_sha256','_crankwave_renderer_copy_source_closure_sha256','_crankwave_compile_scenario_json','_crankwave_destroy_scenario','_crankwave_scenario_copy_id','_crankwave_create_session','_crankwave_destroy_session','_crankwave_session_get_descriptor','_crankwave_session_copy_identity','_crankwave_session_get_audio_bus_descriptor','_crankwave_session_copy_audio_bus_id','_crankwave_session_get_forward_gear_descriptor','_crankwave_session_copy_forward_gear_semantic_id','_crankwave_session_enqueue_controls','_crankwave_session_process']"
    )
    crankwave_enable_renderer_numeric_policy(crankwave_wasm)
    crankwave_configure_wasm_numeric_target(crankwave_wasm)

    add_executable(
        crankwave_wasm_baker
        src/wasm/module.cpp
    )
    set_target_properties(
        crankwave_wasm_baker
        PROPERTIES OUTPUT_NAME crankwave-baker
    )
    target_link_libraries(
        crankwave_wasm_baker
        PRIVATE
            crankwave::c_api
            crankwave::bake_c_api
    )
    target_link_options(
        crankwave_wasm_baker
        PRIVATE
            --no-entry
            -fwasm-exceptions
            "SHELL:-sMODULARIZE=1"
            "SHELL:-sEXPORT_ES6=1"
            "SHELL:-sENVIRONMENT=web,worker,node"
            "SHELL:-sINCOMING_MODULE_JS_API=['locateFile','wasmBinary']"
            "SHELL:-sFILESYSTEM=0"
            "SHELL:-sALLOW_MEMORY_GROWTH=1"
            "SHELL:-sINITIAL_MEMORY=268435456"
            "SHELL:-sMAXIMUM_MEMORY=2147483648"
            "SHELL:-sSTACK_SIZE=1048576"
            "SHELL:-sABORTING_MALLOC=0"
            "SHELL:-sEXIT_RUNTIME=0"
            "SHELL:-sSTRICT=1"
            "SHELL:-sWASM_BIGINT=1"
            "SHELL:-sEXPORTED_RUNTIME_METHODS=['HEAPU8','HEAPU32']"
            "SHELL:-sEXPORTED_FUNCTIONS=['_malloc','_free','_crankwave_api_version','_crankwave_get_abi_layout','_crankwave_context_create','_crankwave_context_destroy','_crankwave_context_get_last_error','_crankwave_context_copy_last_error_text','_crankwave_context_get_diagnostic','_crankwave_context_copy_diagnostic_text','_crankwave_context_get_related_diagnostic','_crankwave_context_copy_related_diagnostic_text','_crankwave_bake_package','_crankwave_destroy_package','_crankwave_package_get_descriptor','_crankwave_package_copy_identity','_crankwave_package_copy_bytes']"
    )
    crankwave_enable_renderer_numeric_policy(
        crankwave_wasm_baker
    )
    crankwave_configure_wasm_numeric_target(
        crankwave_wasm_baker
    )

    include(cmake/CrankwaveWorkbench.cmake)
    crankwave_add_web_workbench(crankwave_wasm)
endif()

add_library(
    crankwave_execution
    STATIC
        src/execution/linux_execution_facts.cpp
)

target_include_directories(
    crankwave_execution
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_execution
    PUBLIC crankwave::contract
    PRIVATE crankwave::build_options
)
crankwave_enable_renderer_numeric_policy(crankwave_execution)

include(cmake/RendererSourceStamp.cmake)

add_library(
    crankwave_renderer_determinism
    STATIC
        src/determinism/loaded_runtime_identity.cpp
        src/determinism/renderer_determinism_envelope.cpp
        src/determinism/renderer_numeric_environment.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    target_sources(
        crankwave_renderer_determinism
        PRIVATE
            src/determinism/linux_loaded_provider.cpp
            src/determinism/linux_loaded_provider_file.cpp
    )
endif()
crankwave_add_renderer_source_stamp(
    crankwave_renderer_determinism
)

target_include_directories(
    crankwave_renderer_determinism
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(
    crankwave_renderer_determinism
    PUBLIC crankwave::contract
    PRIVATE
        crankwave::build_options
        ${CMAKE_DL_LIBS}
)
crankwave_enable_renderer_numeric_policy(
    crankwave_renderer_determinism
)

target_link_libraries(
    crankwave_c_api
    PRIVATE crankwave_renderer_determinism
)

target_link_libraries(
    crankwave_bake
    PRIVATE
        crankwave::compile
        crankwave::simulation
        crankwave::excitation
        crankwave::presentation
        crankwave::session
        crankwave::artifacts
        crankwave_execution
        crankwave_renderer_determinism
)

if(CRANKWAVE_BUILD_TESTS)
    add_library(
        crankwave_reference_parity_v1_reader
        STATIC
            src/reference/reference_parity_v1_reader.cpp
    )

    target_include_directories(
        crankwave_reference_parity_v1_reader
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        crankwave_reference_parity_v1_reader
        PUBLIC crankwave::build_options
        PRIVATE crankwave::contract
    )
    crankwave_enable_renderer_numeric_policy(
        crankwave_reference_parity_v1_reader
    )

    add_library(
        crankwave_p18_reference_catalog
        STATIC
            src/reference/p18_reference_catalog.cpp
    )

    target_include_directories(
        crankwave_p18_reference_catalog
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        crankwave_p18_reference_catalog
        PUBLIC
            crankwave::contract
            crankwave::build_options
    )
    crankwave_enable_renderer_numeric_policy(
        crankwave_p18_reference_catalog
    )

    add_library(
        crankwave_p18_reference_fixture
        STATIC
            src/reference/p18_reference_audit_reader.cpp
            src/reference/p18_reference_fixture_loader.cpp
            src/reference/p18_reference_seed_reader.cpp
    )

    target_include_directories(
        crankwave_p18_reference_fixture
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    )

    target_link_libraries(
        crankwave_p18_reference_fixture
        PUBLIC
            crankwave::artifacts
            crankwave::presentation
            crankwave_p18_reference_catalog
        PRIVATE crankwave::build_options
    )
    crankwave_enable_renderer_numeric_policy(
        crankwave_p18_reference_fixture
    )

endif()

if(CRANKWAVE_BUILD_CLI)
    include(GNUInstallDirs)
    find_package(Git QUIET)
    find_program(
        _crankwave_distribution_tar_executable
        NAMES tar
    )
    set(
        CRANKWAVE_DISTRIBUTION_OUTPUT_DIRECTORY
        "${PROJECT_BINARY_DIR}/distribution"
        CACHE PATH
        "Directory receiving the immutable installed release archive"
    )
    cmake_path(
        ABSOLUTE_PATH CRANKWAVE_DISTRIBUTION_OUTPUT_DIRECTORY
        BASE_DIRECTORY "${PROJECT_BINARY_DIR}"
        NORMALIZE
        OUTPUT_VARIABLE _crankwave_distribution_output_directory
    )

    set(
        _crankwave_install_resource_directory
        "${CMAKE_INSTALL_DATADIR}/crankwave/${CRANKWAVE_RELEASE_IDENTITY}"
    )
    file(
        RELATIVE_PATH
        CRANKWAVE_INSTALL_RESOURCE_RELATIVE_FROM_BINDIR
        "${CMAKE_INSTALL_FULL_BINDIR}"
        "${CMAKE_INSTALL_FULL_DATADIR}/crankwave/${CRANKWAVE_RELEASE_IDENTITY}"
    )
    file(
        RELATIVE_PATH
        CRANKWAVE_INSTALL_ASSET_RELATIVE_FROM_BINDIR
        "${CMAKE_INSTALL_FULL_BINDIR}"
        "${CMAKE_INSTALL_FULL_DATADIR}/crankwave/${CRANKWAVE_RELEASE_IDENTITY}/assets"
    )
    file(
        TO_CMAKE_PATH
        "${CRANKWAVE_INSTALL_RESOURCE_RELATIVE_FROM_BINDIR}"
        CRANKWAVE_INSTALL_RESOURCE_RELATIVE_FROM_BINDIR
    )
    file(
        TO_CMAKE_PATH
        "${CRANKWAVE_INSTALL_ASSET_RELATIVE_FROM_BINDIR}"
        CRANKWAVE_INSTALL_ASSET_RELATIVE_FROM_BINDIR
    )
    set(
        _crankwave_installed_generated_directory
        "${PROJECT_BINARY_DIR}/generated/installed-resources"
    )
    set(
        _crankwave_release_source_inputs_file
        "${_crankwave_installed_generated_directory}/release-source-inputs.txt"
    )
    file(MAKE_DIRECTORY "${_crankwave_installed_generated_directory}")
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/CrankwaveInstalledLayout.hpp.in"
        "${_crankwave_installed_generated_directory}/crankwave_installed_layout.hpp"
        @ONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/CrankwaveInstalledResources.package.json.in"
        "${_crankwave_installed_generated_directory}/package.json"
        @ONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/CrankwaveInstalledRelease.js.in"
        "${_crankwave_installed_generated_directory}/release.js"
        @ONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/CrankwaveBakeWorkflowV2.json.in"
        "${_crankwave_installed_generated_directory}/crankwave-bake-workflow.v2.json"
        @ONLY
    )
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/GenerateInstalledDistributionManifest.cmake.in"
        "${_crankwave_installed_generated_directory}/generate-installed-distribution-manifest.cmake"
        @ONLY
    )

    set(
        _crankwave_builtin_asset_catalog_source
        "${PROJECT_SOURCE_DIR}/assets/builtin/catalog.v1.json"
    )
    set(
        _crankwave_ir_authoring_catalog_source
        "${PROJECT_SOURCE_DIR}/assets/builtin/ir-authoring-catalog.v1.json"
    )
    set(
        _crankwave_ir_library_payload_directory
        "${PROJECT_SOURCE_DIR}/assets/builtin/ir-library/payloads"
    )
    file(
        GLOB _crankwave_ir_library_payload_sources
        CONFIGURE_DEPENDS
        "${_crankwave_ir_library_payload_directory}/*.wav"
    )
    list(SORT _crankwave_ir_library_payload_sources)
    set(
        _crankwave_builtin_asset_bundle
        "${PROJECT_BINARY_DIR}/generated/crankwave-assets"
    )
    set(
        _crankwave_shared_starter_source
        "${PROJECT_SOURCE_DIR}/reference/fixtures/responsive-audio/shared-recorded-starter"
    )
    set(
        _crankwave_shared_starter_bundle
        "${_crankwave_builtin_asset_bundle}/runtime-audio/shared-recorded-starter"
    )
    set(
        _crankwave_shared_starter_runtime
        "${_crankwave_shared_starter_source}/runtime.json"
    )
    set(
        _crankwave_shared_starter_audio
        "${_crankwave_shared_starter_source}/audio/recorded-starter.cropped.192000hz.mono.f32le"
    )
    set(
        _crankwave_builtin_asset_payload_specs
        "f2875947eba2ed98a15f45a5d62f6ae7b607b0a56389bad5882d75748ad374b9|reference/fixtures/crankwave-ir-library/presentation/mild_exhaust.wav"
        "0d736bb7065686ae527c8487ef7850762c0381c91125e555228cf4599e4ff68c|reference/fixtures/crankwave-ir-library/presentation/minimal_muffling_01.wav"
        "87d514d95c85d847d961908249dff0e18789b90fab522c5ef9d9f5dee334e62e|data/profiles/raspy-muscle-620-cleanroom/accessory-configurations/raspy-muscle-620-cleanroom-warm-generic-accessories-v1.json"
        "258a1d713ddaa4101a5d67c21264d3818eaf8cfb8917a73ccfeb2b569defa2f3|data/profiles/sequoia-3ur-fe-cleanroom/accessory-configurations/sequoia-3ur-fe-cleanroom-warm-generic-accessories-v1.json"
        "daf329a3a06c52aca49cc80de87ed13da89e8dcb368f5481e6c2a279c50b0f0b|data/engines/kohler-ch750-cleanroom/accessory-configuration.json"
        "7acc842390f7c916269f4d0a7080f3353dfaafbd687195e4e9d12e03333de8c6|data/engines/radial-5-cleanroom/accessory-configuration.json"
        "476dc695945fff4b4d91957d3aff77b0ecacb89affd759cd8da9b944e7a40e75|data/profiles/bmw-m52tub28-cleanroom/accessory-configurations/bmw-m52tub28-cleanroom-warm-generic-accessories-v1.json"
        "1d3196cedf46ee3370088ccc2f72273f3aea00a627e1b276d4c2d08028451470|data/engines/shovelhead-bank-local-heads/accessory-configuration.json"
        "22a458bb5a2b768403637d36dc8e60af0519050187ee524c9131d796eba93355|data/profiles/harley-evolution-1340-cleanroom/accessory-configurations/harley-evolution-1340-cleanroom-warm-generic-accessories-v1.json"
        "a8d4435faed10556d33439a660305955f40d199f904639a2e6c482de3594b01a|data/engines/subaru-ej25-cleanroom/accessory-configuration.json"
        "b77df170c2bed0a33b7d5387e5ee4b57a75a4a22df11615948cf2724c859784a|data/profiles/honda-b18c5-cleanroom/accessory-configurations/honda-b18c5-cleanroom-warm-generic-accessories-v1.json"
        "6e822e04aea523d5980f53241166a43a971c481d03bb9ba033b6bb097b9debc6|data/profiles/bmw-m52b28/accessory-configurations/bmw-m52b28-warm-stock-accessories-v1.json"
    )
    set(_crankwave_builtin_asset_payload_commands)
    set(_crankwave_builtin_asset_payload_sources)
    foreach(
        _crankwave_builtin_asset_payload_spec
        IN LISTS _crankwave_builtin_asset_payload_specs
    )
        string(
            REPLACE "|" ";"
            _crankwave_builtin_asset_payload_parts
            "${_crankwave_builtin_asset_payload_spec}"
        )
        list(
            GET _crankwave_builtin_asset_payload_parts 0
            _crankwave_builtin_asset_payload_sha256
        )
        list(
            GET _crankwave_builtin_asset_payload_parts 1
            _crankwave_builtin_asset_payload_relative_path
        )
        set(
            _crankwave_builtin_asset_payload_source
            "${PROJECT_SOURCE_DIR}/${_crankwave_builtin_asset_payload_relative_path}"
        )
        list(
            APPEND _crankwave_builtin_asset_payload_sources
            "${_crankwave_builtin_asset_payload_source}"
        )
        list(
            APPEND _crankwave_builtin_asset_payload_commands
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${_crankwave_builtin_asset_payload_source}"
                "${_crankwave_builtin_asset_bundle}/payloads/${_crankwave_builtin_asset_payload_sha256}"
        )
    endforeach()
    foreach(
        _crankwave_ir_library_payload_source
        IN LISTS _crankwave_ir_library_payload_sources
    )
        get_filename_component(
            _crankwave_ir_library_payload_name
            "${_crankwave_ir_library_payload_source}"
            NAME_WE
        )
        string(LENGTH "${_crankwave_ir_library_payload_name}"
            _crankwave_ir_library_payload_name_length)
        if(NOT _crankwave_ir_library_payload_name_length EQUAL 64 OR
           NOT _crankwave_ir_library_payload_name MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR
                "IR library payload filename must be <sha256>.wav: ${_crankwave_ir_library_payload_source}")
        endif()
        list(
            APPEND _crankwave_builtin_asset_payload_commands
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${_crankwave_ir_library_payload_source}"
                "${_crankwave_builtin_asset_bundle}/payloads/${_crankwave_ir_library_payload_name}"
        )
    endforeach()
    set(
        _crankwave_builtin_asset_bundle_stamp
        "${_crankwave_builtin_asset_bundle}/.assembled"
    )
    add_custom_command(
        OUTPUT "${_crankwave_builtin_asset_bundle_stamp}"
        COMMAND
            "${CMAKE_COMMAND}" -E remove_directory
            "${_crankwave_builtin_asset_bundle}"
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${_crankwave_builtin_asset_bundle}/payloads"
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${_crankwave_shared_starter_bundle}/audio"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${_crankwave_builtin_asset_catalog_source}"
            "${_crankwave_builtin_asset_bundle}/catalog.v1.json"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${_crankwave_ir_authoring_catalog_source}"
            "${_crankwave_builtin_asset_bundle}/ir-authoring-catalog.v1.json"
        ${_crankwave_builtin_asset_payload_commands}
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${_crankwave_shared_starter_runtime}"
            "${_crankwave_shared_starter_bundle}/runtime.json"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${_crankwave_shared_starter_audio}"
            "${_crankwave_shared_starter_bundle}/audio/recorded-starter.cropped.192000hz.mono.f32le"
        COMMAND
            "${CMAKE_COMMAND}" -E touch
            "${_crankwave_builtin_asset_bundle_stamp}"
        DEPENDS
            "${_crankwave_builtin_asset_catalog_source}"
            "${_crankwave_ir_authoring_catalog_source}"
            ${_crankwave_builtin_asset_payload_sources}
            ${_crankwave_ir_library_payload_sources}
            "${_crankwave_shared_starter_runtime}"
            "${_crankwave_shared_starter_audio}"
        COMMENT "Assembling the built-in engine asset bundle"
        VERBATIM
    )
    add_custom_target(
        crankwave_builtin_assets
        DEPENDS "${_crankwave_builtin_asset_bundle_stamp}"
    )
    set(
        CRANKWAVE_BUILTIN_ASSET_CATALOG_FOR_TESTS
        "${_crankwave_builtin_asset_bundle}/catalog.v1.json"
    )
    set(
        CRANKWAVE_IR_AUTHORING_CATALOG_FOR_TESTS
        "${_crankwave_builtin_asset_bundle}/ir-authoring-catalog.v1.json"
    )

    add_library(
        crankwave_cli_support
        STATIC
            src/cli/bake_crankwave_command.cpp
            src/cli/cli_app.cpp
            src/cli/native_asset_root.cpp
            src/cli/native_builtin_asset_catalog.cpp
            src/cli/native_engine_input.cpp
            src/cli/native_input_files.cpp
            src/cli/native_input_filesystem.cpp
            src/cli/native_output_directory.cpp
            src/cli/native_responsive_bake_identity.cpp
            src/cli/crankwave_cli_support.cpp
    )

    target_include_directories(
        crankwave_cli_support
        PRIVATE
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/cli>
            $<BUILD_INTERFACE:${_crankwave_installed_generated_directory}>
    )

    target_compile_definitions(
        crankwave_cli_support
        PRIVATE
            CRANKWAVE_VERSION_LABEL="${CRANKWAVE_RELEASE_IDENTITY}"
    )

    target_link_libraries(
        crankwave_cli_support
        PUBLIC
            crankwave::build_options
        PRIVATE
            crankwave::artifacts
            crankwave::authoring
            crankwave::compile
            crankwave::bake
            crankwave::responsive
            crankwave_renderer_determinism
    )
    crankwave_enable_renderer_numeric_policy(
        crankwave_cli_support
    )

    add_executable(
        crankwave_cli
        src/cli/main.cpp
    )

    set_target_properties(
        crankwave_cli
        PROPERTIES OUTPUT_NAME crankwave
    )

    target_link_libraries(
        crankwave_cli
        PRIVATE
            crankwave_cli_support
            $<$<PLATFORM_ID:Linux>:m>
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(
            crankwave_cli
            PRIVATE LINKER:--no-as-needed
        )
    endif()
    crankwave_enable_renderer_numeric_policy(
        crankwave_cli
    )
    crankwave_finalize_renderer_numeric_policy(
        crankwave_cli
    )
    add_dependencies(
        crankwave_cli
        crankwave_builtin_assets
    )
    set_property(
        TARGET crankwave_cli
        APPEND PROPERTY LINK_DEPENDS
        "${_crankwave_builtin_asset_bundle_stamp}"
    )
    add_custom_command(
        TARGET crankwave_cli
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E remove_directory
            "$<TARGET_FILE_DIR:crankwave_cli>/crankwave-assets"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory
            "${_crankwave_builtin_asset_bundle}"
            "$<TARGET_FILE_DIR:crankwave_cli>/crankwave-assets"
        COMMENT "Bundling the built-in engine asset bundle beside the CLI"
        VERBATIM
    )

    install(
        TARGETS crankwave_cli
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(
        FILES
            "${_crankwave_installed_generated_directory}/package.json"
        DESTINATION "${_crankwave_install_resource_directory}"
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION "${_crankwave_install_resource_directory}/licenses"
        RENAME CRANKWAVE.txt
    )
    install(
        FILES "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${_crankwave_install_resource_directory}/licenses"
        RENAME THIRD-PARTY-NOTICES.md
    )
    install(
        FILES
            "${_crankwave_installed_generated_directory}/release.js"
        DESTINATION
            "${_crankwave_install_resource_directory}/web/runtime"
    )
    install(
        FILES
            "${_crankwave_installed_generated_directory}/crankwave-bake-workflow.v2.json"
        DESTINATION
            "${_crankwave_install_resource_directory}/contracts"
    )
    set(
        _crankwave_installed_contract_sources
        "${PROJECT_SOURCE_DIR}/docs/contracts/CLI_RESULT_V1.md"
        "${PROJECT_SOURCE_DIR}/docs/contracts/INSTALLED_DISTRIBUTION_V2.md"
        "${PROJECT_SOURCE_DIR}/docs/contracts/RESPONSIVE_PROFILE_SELECTION_V2.md"
        "${PROJECT_SOURCE_DIR}/docs/contracts/IR_AUTHORING_CATALOG_V1.md"
    )
    install(
        FILES ${_crankwave_installed_contract_sources}
        DESTINATION
            "${_crankwave_install_resource_directory}/docs/contracts"
    )
    file(
        GLOB _crankwave_responsive_profile_sources
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/tools/responsive-audio-baker/profiles/*.json"
    )
    install(
        FILES ${_crankwave_responsive_profile_sources}
        DESTINATION "${_crankwave_install_resource_directory}/profiles"
    )
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/schemas/responsive-audio-bake-profile.schema.json"
            "${PROJECT_SOURCE_DIR}/schemas/ir-authoring-catalog.schema.json"
            "${PROJECT_SOURCE_DIR}/schemas/installed-distribution.v2.schema.json"
            "${PROJECT_SOURCE_DIR}/schemas/crankwave-bake-workflow.v2.schema.json"
        DESTINATION
            "${_crankwave_install_resource_directory}/schemas"
    )
    set(
        _crankwave_javascript_closure
        "${PROJECT_SOURCE_DIR}/web/runtime/directional-phase-cell.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/dry-directional-phase-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/held-phase-texture-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/held-texture-presentation-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/renderer-runtime-compatibility.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/responsive-audio-lifecycle-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/crankwave-audio-engine.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/crankwave-package.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/shared-recorded-starter-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/state-phase-texture-runtime.js"
        "${PROJECT_SOURCE_DIR}/web/runtime/steady-transient-envelope.js"
    )
    install(
        FILES ${_crankwave_javascript_closure}
        DESTINATION
            "${_crankwave_install_resource_directory}/web/runtime"
    )
    set(
        _crankwave_release_source_inputs
        "${PROJECT_SOURCE_DIR}/source-assets.lock.json"
        "${_crankwave_builtin_asset_catalog_source}"
        "${_crankwave_ir_authoring_catalog_source}"
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        ${_crankwave_builtin_asset_payload_sources}
        ${_crankwave_ir_library_payload_sources}
        "${_crankwave_shared_starter_runtime}"
        "${_crankwave_shared_starter_audio}"
        ${_crankwave_responsive_profile_sources}
        "${PROJECT_SOURCE_DIR}/schemas/responsive-audio-bake-profile.schema.json"
        "${PROJECT_SOURCE_DIR}/schemas/ir-authoring-catalog.schema.json"
        "${PROJECT_SOURCE_DIR}/schemas/installed-distribution.v2.schema.json"
        "${PROJECT_SOURCE_DIR}/schemas/crankwave-bake-workflow.v2.schema.json"
        ${_crankwave_javascript_closure}
        ${_crankwave_installed_contract_sources}
        "${PROJECT_SOURCE_DIR}/cmake/CrankwaveInstalledResources.package.json.in"
        "${PROJECT_SOURCE_DIR}/cmake/CrankwaveInstalledRelease.js.in"
        "${PROJECT_SOURCE_DIR}/cmake/CrankwaveBakeWorkflowV2.json.in"
        "${PROJECT_SOURCE_DIR}/cmake/GenerateInstalledDistributionManifest.cmake.in"
    )
    list(REMOVE_DUPLICATES _crankwave_release_source_inputs)
    list(SORT _crankwave_release_source_inputs)
    set(_crankwave_release_source_input_text "")
    foreach(
        _crankwave_release_source_input
        IN LISTS _crankwave_release_source_inputs
    )
        file(
            RELATIVE_PATH _crankwave_release_source_input_relative
            "${PROJECT_SOURCE_DIR}"
            "${_crankwave_release_source_input}"
        )
        file(
            TO_CMAKE_PATH
            "${_crankwave_release_source_input_relative}"
            _crankwave_release_source_input_relative
        )
        if(NOT _crankwave_release_source_input_relative MATCHES
           "^[A-Za-z0-9._+/@-]+$" OR
           _crankwave_release_source_input_relative MATCHES
           "(^|/)\\.\\.(/|$)")
            message(FATAL_ERROR
                "installed release source input is not portable: ${_crankwave_release_source_input_relative}")
        endif()
        string(APPEND _crankwave_release_source_input_text
            "${_crankwave_release_source_input_relative}\n")
    endforeach()
    file(WRITE "${_crankwave_release_source_inputs_file}"
        "${_crankwave_release_source_input_text}")
    install(
        FILES "${_crankwave_builtin_asset_catalog_source}"
        DESTINATION "${_crankwave_install_resource_directory}/assets"
        RENAME catalog.v1.json
    )
    install(
        FILES "${_crankwave_ir_authoring_catalog_source}"
        DESTINATION "${_crankwave_install_resource_directory}/assets"
        RENAME ir-authoring-catalog.v1.json
    )
    install(
        FILES "${_crankwave_shared_starter_runtime}"
        DESTINATION
            "${_crankwave_install_resource_directory}/assets/runtime-audio/shared-recorded-starter"
    )
    install(
        FILES "${_crankwave_shared_starter_audio}"
        DESTINATION
            "${_crankwave_install_resource_directory}/assets/runtime-audio/shared-recorded-starter/audio"
    )
    foreach(
        _crankwave_builtin_asset_payload_spec
        IN LISTS _crankwave_builtin_asset_payload_specs
    )
        string(
            REPLACE "|" ";"
            _crankwave_builtin_asset_payload_parts
            "${_crankwave_builtin_asset_payload_spec}"
        )
        list(
            GET _crankwave_builtin_asset_payload_parts 0
            _crankwave_builtin_asset_payload_sha256
        )
        list(
            GET _crankwave_builtin_asset_payload_parts 1
            _crankwave_builtin_asset_payload_relative_path
        )
        install(
            FILES
                "${PROJECT_SOURCE_DIR}/${_crankwave_builtin_asset_payload_relative_path}"
            DESTINATION
                "${_crankwave_install_resource_directory}/assets/payloads"
            RENAME "${_crankwave_builtin_asset_payload_sha256}"
        )
    endforeach()
    foreach(
        _crankwave_ir_library_payload_source
        IN LISTS _crankwave_ir_library_payload_sources
    )
        get_filename_component(
            _crankwave_ir_library_payload_name
            "${_crankwave_ir_library_payload_source}"
            NAME_WE
        )
        install(
            FILES "${_crankwave_ir_library_payload_source}"
            DESTINATION
                "${_crankwave_install_resource_directory}/assets/payloads"
            RENAME "${_crankwave_ir_library_payload_name}"
        )
    endforeach()

    install(
        SCRIPT
            "${_crankwave_installed_generated_directory}/generate-installed-distribution-manifest.cmake"
    )

    add_custom_target(
        crankwave_distribution
        COMMAND
            "${CMAKE_COMMAND}" -E remove_directory
            "${_crankwave_distribution_output_directory}/$<CONFIG>/stage"
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f
            "${_crankwave_distribution_output_directory}/$<CONFIG>/crankwave-${CRANKWAVE_RELEASE_IDENTITY}.tar"
            "${_crankwave_distribution_output_directory}/$<CONFIG>/crankwave-${CRANKWAVE_RELEASE_IDENTITY}.tar.sha256"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE_STAMP=${PROJECT_BINARY_DIR}/generated/crankwave_generated/renderer_source_stamp_generated.hpp"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
            "-DRELEASE_SOURCE_INPUTS=${_crankwave_release_source_inputs_file}"
            "-DRELEASE_SOURCE_IDENTITY_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/GenerateInstalledDistributionSourceIdentity.cmake"
            -P "${PROJECT_SOURCE_DIR}/cmake/RequireCompleteDistribution.cmake"
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${_crankwave_distribution_output_directory}/$<CONFIG>/stage"
        COMMAND
            "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}"
            --config "$<CONFIG>"
            --prefix
            "${_crankwave_distribution_output_directory}/$<CONFIG>/stage/crankwave-${CRANKWAVE_RELEASE_IDENTITY}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPREFIX_DIRECTORY=${_crankwave_distribution_output_directory}/$<CONFIG>/stage/crankwave-${CRANKWAVE_RELEASE_IDENTITY}"
            "-DRESOURCE_RELATIVE_DIRECTORY=${_crankwave_install_resource_directory}"
            "-DRELEASE_IDENTITY=${CRANKWAVE_RELEASE_IDENTITY}"
            "-DOUTPUT_ARCHIVE=${_crankwave_distribution_output_directory}/$<CONFIG>/crankwave-${CRANKWAVE_RELEASE_IDENTITY}.tar"
            "-DTAR_EXECUTABLE=${_crankwave_distribution_tar_executable}"
            -DREQUIRE_COMPLETE=TRUE
            -P
            "${PROJECT_SOURCE_DIR}/cmake/CreateDeterministicDistributionArchive.cmake"
        DEPENDS
            crankwave_builtin_assets
            crankwave_cli
        BYPRODUCTS
            "${_crankwave_distribution_output_directory}/$<CONFIG>/crankwave-${CRANKWAVE_RELEASE_IDENTITY}.tar"
            "${_crankwave_distribution_output_directory}/$<CONFIG>/crankwave-${CRANKWAVE_RELEASE_IDENTITY}.tar.sha256"
        COMMENT "Assembling the complete immutable crankwave distribution"
        VERBATIM
    )
    set(
        CRANKWAVE_INSTALL_BINDIR_FOR_TESTS
        "${CMAKE_INSTALL_BINDIR}"
    )
    set(
        CRANKWAVE_INSTALL_DATADIR_FOR_TESTS
        "${CMAKE_INSTALL_DATADIR}"
    )
    set(
        CRANKWAVE_RELEASE_IDENTITY_FOR_TESTS
        "${CRANKWAVE_RELEASE_IDENTITY}"
    )
    set(
        CRANKWAVE_DISTRIBUTION_TAR_FOR_TESTS
        "${_crankwave_distribution_tar_executable}"
    )
endif()

set(
    _crankwave_numeric_policy_targets
    crankwave_contract
    crankwave_simulation
    crankwave_bake
    crankwave_artifacts
    crankwave_dsp
    crankwave_presentation
    crankwave_excitation
    crankwave_session
    crankwave_c_api
    crankwave_bake_c_api
    crankwave_execution
    crankwave_renderer_determinism
)
if(TARGET crankwave_wasm)
    list(
        APPEND
        _crankwave_numeric_policy_targets
        crankwave_wasm
        crankwave_wasm_baker
    )
endif()
if(CRANKWAVE_BUILD_TESTS)
    list(
        APPEND
        _crankwave_numeric_policy_targets
        crankwave_reference_parity_v1_reader
        crankwave_p18_reference_catalog
        crankwave_p18_reference_fixture
    )
endif()
if(TARGET crankwave_cli_support)
    list(
        APPEND
        _crankwave_numeric_policy_targets
        crankwave_cli_support
        crankwave_cli
    )
endif()
foreach(_target IN LISTS _crankwave_numeric_policy_targets)
    crankwave_assert_renderer_numeric_policy("${_target}")
endforeach()
if(TARGET crankwave_cli)
    crankwave_assert_final_renderer_numeric_policy(
        crankwave_cli
    )
endif()
unset(_crankwave_numeric_policy_targets)

if(CRANKWAVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
