add_executable(engine_sim_offline_cxx20_smoke cxx20_smoke.cpp)

target_link_libraries(
    engine_sim_offline_cxx20_smoke
    PRIVATE engine_sim_offline::build_options
)

add_test(
    NAME foundation.cxx20
    COMMAND engine_sim_offline_cxx20_smoke
)

add_executable(
    engine_sim_offline_c_api_header_tests
    c_api_header_test.c
)

target_link_libraries(
    engine_sim_offline_c_api_header_tests
    PRIVATE engine_sim_offline::c_api
)

add_test(
    NAME c_api.header_and_context
    COMMAND engine_sim_offline_c_api_header_tests
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(
        engine_sim_offline_c_api_integration_tests
        c_api_integration_test.cpp
    )

    target_link_libraries(
        engine_sim_offline_c_api_integration_tests
        PRIVATE engine_sim_offline::c_api
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_c_api_integration_tests
    )

    add_test(
        NAME c_api.integration
        COMMAND
            engine_sim_offline_c_api_integration_tests
            ${PROJECT_SOURCE_DIR}
    )
    set_tests_properties(
        c_api.integration
        PROPERTIES TIMEOUT 120
    )
endif()

if(EMSCRIPTEN)
    add_executable(
        engine_sim_offline_wasm_numeric_contract_tests
        wasm_numeric_contract_test.cpp
    )

    target_include_directories(
        engine_sim_offline_wasm_numeric_contract_tests
        PRIVATE ${PROJECT_SOURCE_DIR}/src
    )

    target_link_libraries(
        engine_sim_offline_wasm_numeric_contract_tests
        PRIVATE
            engine_sim_offline::contract
            engine_sim_offline::presentation
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_wasm_numeric_contract_tests
    )
    engine_sim_offline_configure_wasm_numeric_target(
        engine_sim_offline_wasm_numeric_contract_tests
    )

    add_test(
        NAME wasm.numeric_contract
        COMMAND engine_sim_offline_wasm_numeric_contract_tests
    )
endif()

if(NOT EMSCRIPTEN)
    add_executable(
        engine_sim_offline_wasm_parity_native
        wasm/native_parity_main.cpp
        wasm/parity_driver.cpp
    )

    target_include_directories(
        engine_sim_offline_wasm_parity_native
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/wasm
    )

    target_link_libraries(
        engine_sim_offline_wasm_parity_native
        PRIVATE
            engine_sim_offline::build_options
            engine_sim_offline::c_api
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_wasm_parity_native
    )
endif()

if(EMSCRIPTEN AND ENGINE_SIM_OFFLINE_BUILD_WASM)
    add_executable(
        engine_sim_offline_wasm_parity_module
        wasm/parity_driver.cpp
    )
    set_target_properties(
        engine_sim_offline_wasm_parity_module
        PROPERTIES
            OUTPUT_NAME engine-sim-offline-wasm-parity
            SUFFIX ".mjs"
    )

    target_include_directories(
        engine_sim_offline_wasm_parity_module
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/wasm
    )

    target_link_libraries(
        engine_sim_offline_wasm_parity_module
        PRIVATE
            engine_sim_offline::build_options
            engine_sim_offline::c_api
    )
    target_link_options(
        engine_sim_offline_wasm_parity_module
        PRIVATE
            --no-entry
            -fwasm-exceptions
            "SHELL:-sMODULARIZE=1"
            "SHELL:-sEXPORT_ES6=1"
            "SHELL:-sENVIRONMENT=node"
            "SHELL:-sFILESYSTEM=0"
            "SHELL:-sALLOW_MEMORY_GROWTH=0"
            "SHELL:-sINITIAL_MEMORY=134217728"
            "SHELL:-sSTACK_SIZE=1048576"
            "SHELL:-sABORTING_MALLOC=0"
            "SHELL:-sEXIT_RUNTIME=0"
            "SHELL:-sSTRICT=1"
            "SHELL:-sWASM_BIGINT=1"
            "SHELL:-sEXPORTED_RUNTIME_METHODS=['HEAPU8','HEAPU32']"
            "SHELL:-sEXPORTED_FUNCTIONS=['_malloc','_free','_eso_wasm_parity_run']"
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_wasm_parity_module
    )
    engine_sim_offline_configure_wasm_numeric_target(
        engine_sim_offline_wasm_parity_module
    )
endif()

add_library(
    engine_sim_offline_authored_engine_fixture_support
    STATIC
        authored_engine_fixture_support.cpp
)

target_include_directories(
    engine_sim_offline_authored_engine_fixture_support
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_authored_engine_fixture_support
    PUBLIC
        engine_sim_offline::authoring
        engine_sim_offline::compile
)

add_executable(
    engine_sim_offline_json_parser_tests
    json_parser_test.cpp
)

target_link_libraries(
    engine_sim_offline_json_parser_tests
    PRIVATE engine_sim_offline::authoring
)

add_test(
    NAME authoring.json_parser
    COMMAND engine_sim_offline_json_parser_tests
)

add_executable(
    engine_sim_offline_authoring_document_tests
    authoring_document_test.cpp
)

target_link_libraries(
    engine_sim_offline_authoring_document_tests
    PRIVATE engine_sim_offline::authoring
)

add_test(
    NAME authoring.document
    COMMAND engine_sim_offline_authoring_document_tests
)

add_executable(
    engine_sim_offline_compiler_foundation_tests
    compiler_foundation_test.cpp
)

target_include_directories(
    engine_sim_offline_compiler_foundation_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_compiler_foundation_tests
    PRIVATE engine_sim_offline::compile
)

add_test(
    NAME compiler.foundation
    COMMAND engine_sim_offline_compiler_foundation_tests
)

add_executable(
    engine_sim_offline_scenario_resolver_tests
    scenario_resolver_test.cpp
)

target_include_directories(
    engine_sim_offline_scenario_resolver_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_scenario_resolver_tests
    PRIVATE engine_sim_offline::compile
)

add_test(
    NAME compiler.scenario_resolver
    COMMAND engine_sim_offline_scenario_resolver_tests
)

add_executable(
    engine_sim_offline_compiler_integration_tests
    compiler_integration_test.cpp
)

target_include_directories(
    engine_sim_offline_compiler_integration_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_compiler_integration_tests
    PRIVATE
        engine_sim_offline::compile
        engine_sim_offline::session
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_compiler_integration_tests
)

add_test(
    NAME compiler.integration
    COMMAND engine_sim_offline_compiler_integration_tests
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(
        engine_sim_offline_engine_session_tests
        engine_session_test.cpp
        bmw_m52b28_render_gate_support.cpp
    )

    target_include_directories(
        engine_sim_offline_engine_session_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )

    target_link_libraries(
        engine_sim_offline_engine_session_tests
        PRIVATE
            engine_sim_offline::compile
            engine_sim_offline::presentation
            engine_sim_offline::session
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_engine_session_tests
    )

    if(NOT CMAKE_CONFIGURATION_TYPES AND
       CMAKE_BUILD_TYPE STREQUAL "Release")
        add_test(
            NAME session.engine_session
            COMMAND
                engine_sim_offline_engine_session_tests
                ${PROJECT_SOURCE_DIR}
        )
        set_tests_properties(
            session.engine_session
            PROPERTIES TIMEOUT 240
        )
    endif()

    add_executable(
        engine_sim_offline_bmw_m52b28_authored_json_render_gate_tests
        bmw_m52b28_authored_json_render_gate_test.cpp
        bmw_m52b28_render_gate_support.cpp
        bmw_m52b28_render_gate_wave.cpp
    )

    target_link_libraries(
        engine_sim_offline_bmw_m52b28_authored_json_render_gate_tests
        PRIVATE
            engine_sim_offline::compile
            engine_sim_offline::bake
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_bmw_m52b28_authored_json_render_gate_tests
    )

    if(NOT CMAKE_CONFIGURATION_TYPES AND
       CMAKE_BUILD_TYPE STREQUAL "Release")
        add_test(
            NAME render.bmw_m52b28_authored_json_migration
            COMMAND
                engine_sim_offline_bmw_m52b28_authored_json_render_gate_tests
                ${PROJECT_SOURCE_DIR}
        )
        set_tests_properties(
            render.bmw_m52b28_authored_json_migration
            PROPERTIES TIMEOUT 240
        )
    endif()
endif()

add_executable(
    engine_sim_offline_renderer_source_stamp_tests
    renderer_source_stamp_test.cpp
)

target_link_libraries(
    engine_sim_offline_renderer_source_stamp_tests
    PRIVATE engine_sim_offline_renderer_determinism
)

add_test(
    NAME determinism.renderer_source_stamp
    COMMAND engine_sim_offline_renderer_source_stamp_tests
)

add_executable(
    engine_sim_offline_loaded_runtime_identity_tests
    loaded_runtime_identity_test.cpp
)

target_link_options(
    engine_sim_offline_loaded_runtime_identity_tests
    PRIVATE $<$<PLATFORM_ID:Linux>:LINKER:--no-as-needed>
)

target_link_libraries(
    engine_sim_offline_loaded_runtime_identity_tests
    PRIVATE
        engine_sim_offline_renderer_determinism
        $<$<PLATFORM_ID:Linux>:m>
)

add_test(
    NAME determinism.loaded_runtime_identity
    COMMAND engine_sim_offline_loaded_runtime_identity_tests
)

engine_sim_offline_renderer_numeric_policy_is_admitted(
    _engine_sim_offline_numeric_policy_admitted
)
if(_engine_sim_offline_numeric_policy_admitted)
    find_package(Threads REQUIRED)

    add_executable(
        engine_sim_offline_renderer_numeric_environment_tests
        renderer_numeric_environment_test.cpp
    )

    target_link_libraries(
        engine_sim_offline_renderer_numeric_environment_tests
        PRIVATE engine_sim_offline_renderer_determinism
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_renderer_numeric_environment_tests
    )

    add_test(
        NAME determinism.renderer_numeric_environment
        COMMAND engine_sim_offline_renderer_numeric_environment_tests
    )

    add_executable(
        engine_sim_offline_renderer_determinism_envelope_tests
        renderer_determinism_envelope_test.cpp
    )

    target_link_options(
        engine_sim_offline_renderer_determinism_envelope_tests
        PRIVATE $<$<PLATFORM_ID:Linux>:LINKER:--no-as-needed>
    )

    target_link_libraries(
        engine_sim_offline_renderer_determinism_envelope_tests
        PRIVATE
            engine_sim_offline_renderer_determinism
            Threads::Threads
            $<$<PLATFORM_ID:Linux>:m>
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_renderer_determinism_envelope_tests
    )

    add_test(
        NAME determinism.renderer_determinism_envelope
        COMMAND engine_sim_offline_renderer_determinism_envelope_tests
    )
endif()
unset(_engine_sim_offline_numeric_policy_admitted)

add_executable(
    engine_sim_offline_renderer_numeric_policy_consumer_tests
    renderer_numeric_policy_consumer_test.cpp
)

target_link_libraries(
    engine_sim_offline_renderer_numeric_policy_consumer_tests
    PRIVATE engine_sim_offline::bake
)

add_test(
    NAME determinism.renderer_numeric_policy_private
    COMMAND engine_sim_offline_renderer_numeric_policy_consumer_tests
)

get_target_property(
    _engine_sim_offline_numeric_flag_text
    engine_sim_offline_renderer_numeric_policy
    ENGINE_SIM_OFFLINE_NUMERIC_POLICY_FLAG_TEXT
)
set(ENGINE_SIM_OFFLINE_RENDERER_NUMERIC_POLICY_ADMITTED 0)
set(
    ENGINE_SIM_OFFLINE_RENDERER_NUMERIC_POLICY_FLAGS
    "${_engine_sim_offline_numeric_flag_text}"
)
set(
    _engine_sim_offline_unmarked_generated_directory
    "${CMAKE_CURRENT_BINARY_DIR}/numeric-policy-unmarked/generated"
)
file(
    MAKE_DIRECTORY
    "${_engine_sim_offline_unmarked_generated_directory}/engine_sim_offline_generated"
)
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/RendererNumericPolicyGenerated.hpp.in"
    "${_engine_sim_offline_unmarked_generated_directory}/engine_sim_offline_generated/renderer_numeric_policy_generated.hpp"
    @ONLY
)

add_executable(
    engine_sim_offline_renderer_numeric_policy_unmarked_tests
    renderer_numeric_policy_unmarked_test.cpp
    ${PROJECT_SOURCE_DIR}/src/determinism/renderer_numeric_environment.cpp
)

target_include_directories(
    engine_sim_offline_renderer_numeric_policy_unmarked_tests
    BEFORE PRIVATE
        "${_engine_sim_offline_unmarked_generated_directory}"
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_renderer_numeric_policy_unmarked_tests
    PRIVATE engine_sim_offline::build_options
)

add_test(
    NAME determinism.renderer_numeric_policy_unmarked
    COMMAND engine_sim_offline_renderer_numeric_policy_unmarked_tests
)

unset(_engine_sim_offline_numeric_flag_text)
unset(_engine_sim_offline_unmarked_generated_directory)
unset(ENGINE_SIM_OFFLINE_RENDERER_NUMERIC_POLICY_ADMITTED)
unset(ENGINE_SIM_OFFLINE_RENDERER_NUMERIC_POLICY_FLAGS)

set(_engine_sim_offline_sanitized FALSE)
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=" OR
   CMAKE_CXX_FLAGS_RELEASE MATCHES "-fsanitize=" OR
   CMAKE_CXX_FLAGS_DEBUG MATCHES "-fsanitize=")
    set(_engine_sim_offline_sanitized TRUE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND
   CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$" AND
   NOT _engine_sim_offline_sanitized)
    add_library(
        engine_sim_offline_loaded_runtime_tan_interposer
        SHARED loaded_runtime_tan_interposer.cpp
    )

    add_test(
        NAME determinism.loaded_runtime_interposition
        COMMAND
            ${CMAKE_COMMAND} -E env
            "LD_PRELOAD=$<TARGET_FILE:engine_sim_offline_loaded_runtime_tan_interposer>"
            ENGINE_SIM_OFFLINE_TEST_EXPECT_TAN_INTERPOSITION=1
            $<TARGET_FILE:engine_sim_offline_loaded_runtime_identity_tests>
    )

    add_executable(
        engine_sim_offline_loaded_runtime_static_tests
        loaded_runtime_identity_test.cpp
    )

    target_compile_definitions(
        engine_sim_offline_loaded_runtime_static_tests
        PRIVATE ENGINE_SIM_OFFLINE_TEST_STATIC_RUNTIME=1
    )

    target_link_options(
        engine_sim_offline_loaded_runtime_static_tests
        PRIVATE
            -static-libstdc++
            -static-libgcc
            LINKER:--no-as-needed
    )

    target_link_libraries(
        engine_sim_offline_loaded_runtime_static_tests
        PRIVATE
            engine_sim_offline_renderer_determinism
            m
    )

    add_test(
        NAME determinism.loaded_runtime_static_rejection
        COMMAND engine_sim_offline_loaded_runtime_static_tests
    )
endif()

find_package(Git QUIET)
if(GIT_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    add_test(
        NAME determinism.renderer_source_stamp_generator
        COMMAND
            ${CMAKE_COMMAND}
            "-DGENERATOR_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/GenerateRendererSourceStamp.cmake"
            "-DTEST_ROOT=${PROJECT_BINARY_DIR}/tests/renderer-source-stamp-generator"
            "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
            "-DTEST_COMPILER_EXECUTABLE=${CMAKE_CXX_COMPILER}"
            "-DTEST_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
            "-DTEST_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}"
            -P "${PROJECT_SOURCE_DIR}/tests/renderer_source_stamp_cmake_test.cmake"
    )
endif()

add_executable(
    engine_sim_offline_contract_tests
    authored_profile_contract_test.cpp
    capture_contract_test.cpp
    contract_test_main.cpp
    parity_model_contract_test.cpp
    primitives_contract_test.cpp
    randomness_contract_test.cpp
    scenario_manifest_contract_test.cpp
)

target_link_libraries(
    engine_sim_offline_contract_tests
    PRIVATE engine_sim_offline::contract
)

add_test(
    NAME contract.data_model
    COMMAND engine_sim_offline_contract_tests
)

add_executable(
    engine_sim_offline_render_job_derivation_tests
    render_job_derivation_test.cpp
)

target_include_directories(
    engine_sim_offline_render_job_derivation_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_render_job_derivation_tests
    PRIVATE
        engine_sim_offline::bake
        engine_sim_offline::presentation
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_render_job_derivation_tests
)

add_test(
    NAME render.job_derivation
    COMMAND engine_sim_offline_render_job_derivation_tests
)

add_executable(
    engine_sim_offline_artifact_serialization_tests
    artifact_serialization_test.cpp
)

target_include_directories(
    engine_sim_offline_artifact_serialization_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/artifacts
)

target_link_libraries(
    engine_sim_offline_artifact_serialization_tests
    PRIVATE engine_sim_offline::artifacts
)

add_test(
    NAME artifacts.serialization
    COMMAND engine_sim_offline_artifact_serialization_tests
)

add_executable(
    engine_sim_offline_simulation_manifest_encoder_tests
    simulation_manifest_encoder_test.cpp
)

target_include_directories(
    engine_sim_offline_simulation_manifest_encoder_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_simulation_manifest_encoder_tests
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline::request_identity
)

add_test(
    NAME artifacts.simulation_manifest_encoder
    COMMAND engine_sim_offline_simulation_manifest_encoder_tests
)

add_executable(
    engine_sim_offline_p18_reference_catalog_tests
    p18_reference_catalog_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_reference_catalog_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_reference_catalog_tests
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline::presentation
        engine_sim_offline_p18_reference_catalog
)

add_test(
    NAME reference.p18_catalog
    COMMAND engine_sim_offline_p18_reference_catalog_tests
)

add_executable(
    engine_sim_offline_directory_render_sink_tests
    directory_render_sink_test.cpp
)

target_link_libraries(
    engine_sim_offline_directory_render_sink_tests
    PRIVATE engine_sim_offline::artifacts
)

add_test(
    NAME artifacts.directory_sink
    COMMAND engine_sim_offline_directory_render_sink_tests
)

add_executable(
    engine_sim_offline_dsp_primitive_tests
    dsp_primitives_test.cpp
)

target_include_directories(
    engine_sim_offline_dsp_primitive_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_dsp_primitive_tests
    PRIVATE engine_sim_offline::dsp
)

add_test(
    NAME dsp.primitives
    COMMAND engine_sim_offline_dsp_primitive_tests
)

add_executable(
    engine_sim_offline_random_table_tests
    random_table_test.cpp
)

target_include_directories(
    engine_sim_offline_random_table_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_random_table_tests
    PRIVATE engine_sim_offline::dsp
)

add_test(
    NAME dsp.random_table
    COMMAND engine_sim_offline_random_table_tests
)

add_executable(
    engine_sim_offline_component_seed_derivation_tests
    component_seed_derivation_test.cpp
)

target_link_libraries(
    engine_sim_offline_component_seed_derivation_tests
    PRIVATE engine_sim_offline::contract
)

add_test(
    NAME randomness.component_seed_derivation
    COMMAND engine_sim_offline_component_seed_derivation_tests
)

add_executable(
    engine_sim_offline_causal_reconstruction_tests
    causal_reconstruction_test.cpp
)

target_include_directories(
    engine_sim_offline_causal_reconstruction_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_causal_reconstruction_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.causal_reconstruction
    COMMAND engine_sim_offline_causal_reconstruction_tests
)

add_executable(
    engine_sim_offline_route_conditioning_tests
    route_conditioning_test.cpp
)

target_include_directories(
    engine_sim_offline_route_conditioning_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_route_conditioning_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.route_conditioning
    COMMAND engine_sim_offline_route_conditioning_tests
)

add_executable(
    engine_sim_offline_exhaust_source_stage_tests
    exhaust_source_stage_test.cpp
)

target_include_directories(
    engine_sim_offline_exhaust_source_stage_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_exhaust_source_stage_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.exhaust_source_stage
    COMMAND engine_sim_offline_exhaust_source_stage_tests
)

add_executable(
    engine_sim_offline_pcm16_ir_decoder_tests
    pcm16_ir_decoder_test.cpp
)

target_include_directories(
    engine_sim_offline_pcm16_ir_decoder_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_pcm16_ir_decoder_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.pcm16_ir_decoder
    COMMAND engine_sim_offline_pcm16_ir_decoder_tests
)

add_executable(
    engine_sim_offline_static_ir_conversion_tests
    static_ir_conversion_test.cpp
)

target_include_directories(
    engine_sim_offline_static_ir_conversion_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_static_ir_conversion_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.static_ir_conversion
    COMMAND
        engine_sim_offline_static_ir_conversion_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav
)

add_executable(
    engine_sim_offline_presentation_method_registry_tests
    presentation_method_registry_test.cpp
)

target_include_directories(
    engine_sim_offline_presentation_method_registry_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_presentation_method_registry_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.method_registry
    COMMAND engine_sim_offline_presentation_method_registry_tests
)

add_executable(
    engine_sim_offline_presentation_calibration_compiler_tests
    presentation_calibration_compiler_test.cpp
)

target_include_directories(
    engine_sim_offline_presentation_calibration_compiler_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_presentation_calibration_compiler_tests
    PRIVATE engine_sim_offline::presentation
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_presentation_calibration_compiler_tests
)

add_test(
    NAME presentation.calibration_compiler
    COMMAND engine_sim_offline_presentation_calibration_compiler_tests
)

add_executable(
    engine_sim_offline_presentation_asset_compiler_tests
    presentation_asset_compiler_test.cpp
)

target_include_directories(
    engine_sim_offline_presentation_asset_compiler_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_presentation_asset_compiler_tests
    PRIVATE
        engine_sim_offline::presentation
        $<$<PLATFORM_ID:Linux>:m>
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_presentation_asset_compiler_tests
)

add_test(
    NAME presentation.asset_compiler
    COMMAND
        engine_sim_offline_presentation_asset_compiler_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav
)

add_executable(
    engine_sim_offline_fixed_fft_convolution_tests
    fixed_fft_convolution_test.cpp
)

target_include_directories(
    engine_sim_offline_fixed_fft_convolution_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_fixed_fft_convolution_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.fixed_fft_convolution
    COMMAND engine_sim_offline_fixed_fft_convolution_tests
)

add_executable(
    engine_sim_offline_presentation_audio_session_tests
    presentation_audio_session_test.cpp
)

target_include_directories(
    engine_sim_offline_presentation_audio_session_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_presentation_audio_session_tests
    PRIVATE engine_sim_offline::presentation
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_presentation_audio_session_tests
)

add_test(
    NAME presentation.audio_session
    COMMAND engine_sim_offline_presentation_audio_session_tests
)

add_executable(
    engine_sim_offline_session_control_timeline_tests
    session_control_timeline_test.cpp
)

target_include_directories(
    engine_sim_offline_session_control_timeline_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_session_control_timeline_tests
    PRIVATE engine_sim_offline::session
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_session_control_timeline_tests
)

add_test(
    NAME session.control_timeline
    COMMAND engine_sim_offline_session_control_timeline_tests
)

add_executable(
    engine_sim_offline_p18_reference_audit_reader_tests
    p18_reference_audit_reader_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_reference_audit_reader_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_reference_audit_reader_tests
    PRIVATE engine_sim_offline_p18_reference_fixture
)

add_test(
    NAME reference.p18_audit_reader
    COMMAND
        engine_sim_offline_p18_reference_audit_reader_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/reference-audit.bin
)

add_executable(
    engine_sim_offline_reference_parity_v1_reader_tests
    reference_parity_v1_reader_test.cpp
)

target_link_libraries(
    engine_sim_offline_reference_parity_v1_reader_tests
    PRIVATE engine_sim_offline_reference_parity_v1_reader
)

add_test(
    NAME reference.parity_v1_reader
    COMMAND
        engine_sim_offline_reference_parity_v1_reader_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/reference-parity.bin
)

add_executable(
    engine_sim_offline_legacy_low_order_mechanics_tests
    legacy_low_order_mechanics_test.cpp
)

target_include_directories(
    engine_sim_offline_legacy_low_order_mechanics_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_legacy_low_order_mechanics_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.legacy_low_order_mechanics
    COMMAND engine_sim_offline_legacy_low_order_mechanics_tests
)

add_executable(
    engine_sim_offline_centered_slider_crank_equivalent_inertia_tests
    centered_slider_crank_equivalent_inertia_test.cpp
)

target_include_directories(
    engine_sim_offline_centered_slider_crank_equivalent_inertia_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_centered_slider_crank_equivalent_inertia_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.centered_slider_crank_equivalent_inertia
    COMMAND engine_sim_offline_centered_slider_crank_equivalent_inertia_tests
)

add_executable(
    engine_sim_offline_kinematic_scenario_schedule_tests
    kinematic_scenario_schedule_test.cpp
)

target_include_directories(
    engine_sim_offline_kinematic_scenario_schedule_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_kinematic_scenario_schedule_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.kinematic_scenario_schedule
    COMMAND engine_sim_offline_kinematic_scenario_schedule_tests
)

add_executable(
    engine_sim_offline_four_stroke_cycle_integrator_tests
    four_stroke_cycle_integrator_test.cpp
)

target_include_directories(
    engine_sim_offline_four_stroke_cycle_integrator_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_four_stroke_cycle_integrator_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.four_stroke_cycle_integrator
    COMMAND engine_sim_offline_four_stroke_cycle_integrator_tests
)

add_executable(
    engine_sim_offline_chen_flynn_cycle_mean_loss_tests
    chen_flynn_cycle_mean_loss_test.cpp
)

target_include_directories(
    engine_sim_offline_chen_flynn_cycle_mean_loss_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_chen_flynn_cycle_mean_loss_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.chen_flynn_cycle_mean_loss
    COMMAND engine_sim_offline_chen_flynn_cycle_mean_loss_tests
)

add_executable(
    engine_sim_offline_fixed_horizon_cycle_sampling_tests
    fixed_horizon_cycle_sampling_test.cpp
)

target_include_directories(
    engine_sim_offline_fixed_horizon_cycle_sampling_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_fixed_horizon_cycle_sampling_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.fixed_horizon_cycle_sampling
    COMMAND engine_sim_offline_fixed_horizon_cycle_sampling_tests
)

add_executable(
    engine_sim_offline_held_speed_operating_point_result_tests
    held_speed_operating_point_result_test.cpp
)

target_link_libraries(
    engine_sim_offline_held_speed_operating_point_result_tests
    PRIVATE
        engine_sim_offline_authored_engine_fixture_support
        engine_sim_offline::request_identity
        engine_sim_offline::simulation
)

add_test(
    NAME contract.held_speed_operating_point_result
    COMMAND
        engine_sim_offline_held_speed_operating_point_result_tests
        ${PROJECT_SOURCE_DIR}
)

add_executable(
    engine_sim_offline_inertial_dyno_result_contract_tests
    inertial_dyno_result_contract_test.cpp
)

target_link_libraries(
    engine_sim_offline_inertial_dyno_result_contract_tests
    PRIVATE engine_sim_offline::contract
)

add_test(
    NAME contract.inertial_dyno_result
    COMMAND engine_sim_offline_inertial_dyno_result_contract_tests
)

add_executable(
    engine_sim_offline_operating_cycle_accountant_tests
    operating_cycle_accountant_test.cpp
)

target_include_directories(
    engine_sim_offline_operating_cycle_accountant_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_operating_cycle_accountant_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.operating_cycle_accountant
    COMMAND engine_sim_offline_operating_cycle_accountant_tests
)

add_executable(
    engine_sim_offline_inertial_crank_dynamics_tests
    inertial_crank_dynamics_test.cpp
)

target_include_directories(
    engine_sim_offline_inertial_crank_dynamics_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_inertial_crank_dynamics_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.inertial_crank_dynamics
    COMMAND engine_sim_offline_inertial_crank_dynamics_tests
)

add_executable(
    engine_sim_offline_engine_sim_v1_transient_friction_tests
    engine_sim_v1_transient_friction_test.cpp
)

target_include_directories(
    engine_sim_offline_engine_sim_v1_transient_friction_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_engine_sim_v1_transient_friction_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.engine_sim_v1_transient_friction
    COMMAND engine_sim_offline_engine_sim_v1_transient_friction_tests
)

add_executable(
    engine_sim_offline_engine_sim_v1_starter_motor_tests
    engine_sim_v1_starter_motor_test.cpp
)

target_include_directories(
    engine_sim_offline_engine_sim_v1_starter_motor_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_engine_sim_v1_starter_motor_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.engine_sim_v1_starter_motor
    COMMAND engine_sim_offline_engine_sim_v1_starter_motor_tests
)

add_executable(
    engine_sim_offline_cycle_accounting_method_registry_tests
    cycle_accounting_method_registry_test.cpp
)

target_include_directories(
    engine_sim_offline_cycle_accounting_method_registry_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_cycle_accounting_method_registry_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.cycle_accounting_method_registry
    COMMAND engine_sim_offline_cycle_accounting_method_registry_tests
)

add_executable(
    engine_sim_offline_legacy_fixed_valvetrain_tests
    legacy_fixed_valvetrain_test.cpp
)

target_include_directories(
    engine_sim_offline_legacy_fixed_valvetrain_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_legacy_fixed_valvetrain_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.legacy_fixed_valvetrain
    COMMAND engine_sim_offline_legacy_fixed_valvetrain_tests
)

add_executable(
    engine_sim_offline_legacy_gas_primitives_tests
    legacy_gas_primitives_test.cpp
)

target_include_directories(
    engine_sim_offline_legacy_gas_primitives_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_legacy_gas_primitives_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.legacy_gas_primitives
    COMMAND engine_sim_offline_legacy_gas_primitives_tests
)

add_executable(
    engine_sim_offline_legacy_combustion_primitives_tests
    legacy_combustion_primitives_test.cpp
)

target_include_directories(
    engine_sim_offline_legacy_combustion_primitives_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_legacy_combustion_primitives_tests
    PRIVATE engine_sim_offline::simulation
)

add_test(
    NAME simulation.legacy_combustion_primitives
    COMMAND engine_sim_offline_legacy_combustion_primitives_tests
)

add_executable(
    engine_sim_offline_legacy_low_order_gas_tests
    legacy_low_order_gas_test.cpp
)

target_include_directories(
    engine_sim_offline_legacy_low_order_gas_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_legacy_low_order_gas_tests
    PRIVATE
        engine_sim_offline_authored_engine_fixture_support
        engine_sim_offline::simulation
)

add_test(
    NAME simulation.legacy_low_order_gas
    COMMAND
        engine_sim_offline_legacy_low_order_gas_tests
        ${PROJECT_SOURCE_DIR}
)

add_executable(
    engine_sim_offline_low_order_capture_session_tests
    low_order_capture_session_test.cpp
)

target_include_directories(
    engine_sim_offline_low_order_capture_session_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_low_order_capture_session_tests
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline_authored_engine_fixture_support
        engine_sim_offline::simulation
)

add_test(
    NAME simulation.low_order_capture_session
    COMMAND
        engine_sim_offline_low_order_capture_session_tests
        ${PROJECT_SOURCE_DIR}
)

add_executable(
    engine_sim_offline_bmw_m52b28_free_engine_source_parity_tests
    bmw_m52b28_free_engine_source_parity_test.cpp
    bmw_m52b28_render_gate_support.cpp
)

target_include_directories(
    engine_sim_offline_bmw_m52b28_free_engine_source_parity_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_bmw_m52b28_free_engine_source_parity_tests
    PRIVATE
        engine_sim_offline::bake
        engine_sim_offline::compile
        engine_sim_offline::simulation
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_bmw_m52b28_free_engine_source_parity_tests
)

add_test(
    NAME simulation.bmw_m52b28_free_engine_source_parity
    COMMAND
        engine_sim_offline_bmw_m52b28_free_engine_source_parity_tests
        ${PROJECT_SOURCE_DIR}
)
set_tests_properties(
    simulation.bmw_m52b28_free_engine_source_parity
    PROPERTIES TIMEOUT 240
)

add_executable(
    engine_sim_offline_low_order_capture_plan_tests
    low_order_capture_plan_test.cpp
)

target_include_directories(
    engine_sim_offline_low_order_capture_plan_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_low_order_capture_plan_tests
    PRIVATE
        engine_sim_offline_authored_engine_fixture_support
        engine_sim_offline::simulation
)

add_test(
    NAME simulation.low_order_capture_plan
    COMMAND
        engine_sim_offline_low_order_capture_plan_tests
        ${PROJECT_SOURCE_DIR}
)

add_executable(
    engine_sim_offline_low_order_engine_core_v1_runtime_tests
    low_order_engine_core_v1_runtime_test.cpp
)

target_include_directories(
    engine_sim_offline_low_order_engine_core_v1_runtime_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_low_order_engine_core_v1_runtime_tests
    PRIVATE
        engine_sim_offline_authored_engine_fixture_support
        engine_sim_offline::simulation
)

add_test(
    NAME simulation.low_order_engine_core_v1_runtime
    COMMAND
        engine_sim_offline_low_order_engine_core_v1_runtime_tests
        ${PROJECT_SOURCE_DIR}
)

add_executable(
    engine_sim_offline_low_order_operating_point_v1_runtime_tests
    low_order_operating_point_v1_runtime_test.cpp
)

target_include_directories(
    engine_sim_offline_low_order_operating_point_v1_runtime_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_low_order_operating_point_v1_runtime_tests
    PRIVATE
        engine_sim_offline_authored_engine_fixture_support
        engine_sim_offline::simulation
)

add_test(
    NAME simulation.low_order_operating_point_v1_runtime
    COMMAND
        engine_sim_offline_low_order_operating_point_v1_runtime_tests
        ${PROJECT_SOURCE_DIR}
)

add_executable(
    engine_sim_offline_legacy_low_order_exhaust_excitation_tests
    legacy_low_order_exhaust_excitation_test.cpp
)

target_include_directories(
    engine_sim_offline_legacy_low_order_exhaust_excitation_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_legacy_low_order_exhaust_excitation_tests
    PRIVATE
        engine_sim_offline::excitation
        engine_sim_offline_authored_engine_fixture_support
)

add_test(
    NAME excitation.legacy_low_order_exhaust
    COMMAND
        engine_sim_offline_legacy_low_order_exhaust_excitation_tests
        ${PROJECT_SOURCE_DIR}
)

add_executable(
    engine_sim_offline_bmw_m52b28_mechanics_parity_tests
    bmw_m52b28_mechanics_parity_test.cpp
)

target_include_directories(
    engine_sim_offline_bmw_m52b28_mechanics_parity_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_bmw_m52b28_mechanics_parity_tests
    PRIVATE
        engine_sim_offline_authored_engine_fixture_support
        engine_sim_offline::simulation
        engine_sim_offline_reference_parity_v1_reader
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_bmw_m52b28_mechanics_parity_tests
)

add_test(
    NAME simulation.bmw_m52b28_mechanics_parity
    COMMAND
        engine_sim_offline_bmw_m52b28_mechanics_parity_tests
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/reference-parity.bin
)

add_executable(
    engine_sim_offline_p18_reference_seed_reader_tests
    p18_reference_seed_reader_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_reference_seed_reader_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_reference_seed_reader_tests
    PRIVATE engine_sim_offline_p18_reference_fixture
)

add_test(
    NAME reference.p18_seed_reader
    COMMAND
        engine_sim_offline_p18_reference_seed_reader_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/component-seeds.bin
)

add_executable(
    engine_sim_offline_p18_reference_fixture_loader_tests
    p18_reference_fixture_loader_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_reference_fixture_loader_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_reference_fixture_loader_tests
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline::presentation
        engine_sim_offline_p18_reference_catalog
        engine_sim_offline_p18_reference_fixture
)

add_test(
    NAME reference.p18_fixture_loader
    COMMAND
        engine_sim_offline_p18_reference_fixture_loader_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18
)

add_executable(
    engine_sim_offline_mastering_tests
    mastering_test.cpp
)

target_include_directories(
    engine_sim_offline_mastering_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_mastering_tests
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline::presentation
)

add_test(
    NAME presentation.mastering
    COMMAND
        engine_sim_offline_mastering_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/presentation/stems/exhaust-0-linear-wet-dry.wav
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/presentation/stems/exhaust-1-linear-wet-dry.wav
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(
        engine_sim_offline_native_presentation_publisher_tests
        native_presentation_publisher_test.cpp
    )

    target_include_directories(
        engine_sim_offline_native_presentation_publisher_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )

    target_link_libraries(
        engine_sim_offline_native_presentation_publisher_tests
        PRIVATE
            engine_sim_offline::bake
            engine_sim_offline::authoring
            engine_sim_offline::session
            $<$<PLATFORM_ID:Linux>:m>
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_native_presentation_publisher_tests
    )

    add_test(
        NAME render.native_presentation_publisher
        COMMAND
            engine_sim_offline_native_presentation_publisher_tests
            ${PROJECT_SOURCE_DIR}
    )

    add_executable(
        engine_sim_offline_linux_execution_facts_tests
        linux_execution_facts_test.cpp
    )

    target_include_directories(
        engine_sim_offline_linux_execution_facts_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )

    target_link_libraries(
        engine_sim_offline_linux_execution_facts_tests
        PRIVATE engine_sim_offline_execution
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_linux_execution_facts_tests
    )

    add_test(
        NAME execution.linux_facts
        COMMAND engine_sim_offline_linux_execution_facts_tests
    )

endif()

if(TARGET engine_sim_offline_cli_support)
    add_executable(
        engine_sim_offline_cli_app_tests
        cli_app_test.cpp
    )

    target_include_directories(
        engine_sim_offline_cli_app_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src/cli
    )

    target_link_libraries(
        engine_sim_offline_cli_app_tests
        PRIVATE engine_sim_offline_cli_support
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_cli_app_tests
    )

    add_test(
        NAME cli.app
        COMMAND engine_sim_offline_cli_app_tests
    )

    add_executable(
        engine_sim_offline_native_input_files_tests
        native_input_files_test.cpp
    )

    target_include_directories(
        engine_sim_offline_native_input_files_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src/cli
    )

    target_link_libraries(
        engine_sim_offline_native_input_files_tests
        PRIVATE
            engine_sim_offline_cli_support
            engine_sim_offline::compile
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_native_input_files_tests
    )

    add_test(
        NAME cli.native_input_files
        COMMAND
            engine_sim_offline_native_input_files_tests
            ${PROJECT_SOURCE_DIR}
    )

    add_test(
        NAME cli.process
        COMMAND
            ${CMAKE_COMMAND}
            -DCLI_EXECUTABLE=$<TARGET_FILE:engine_sim_offline_cli>
            -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_process_test.cmake
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(
            NAME cli.bmw_json_render
            COMMAND
                ${CMAKE_COMMAND}
                -DCLI_EXECUTABLE=$<TARGET_FILE:engine_sim_offline_cli>
                -DSOURCE_ROOT=${PROJECT_SOURCE_DIR}
                -DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-integration-output
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_bmw_json_render_test.cmake
        )
        set_tests_properties(
            cli.bmw_json_render
            PROPERTIES
                LABELS "publication"
                CONFIGURATIONS Release
                TIMEOUT 360
        )
    endif()
endif()
