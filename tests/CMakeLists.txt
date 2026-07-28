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
    PRIVATE engine_sim_offline::render
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
    reference_manifest_contract_test.cpp
    scenario_manifest_contract_test.cpp
)

target_link_libraries(
    engine_sim_offline_contract_tests
    PRIVATE
        engine_sim_offline::contract
        engine_sim_offline_p18_reference_catalog
)

add_test(
    NAME contract.data_model
    COMMAND engine_sim_offline_contract_tests
)

add_executable(
    engine_sim_offline_render_tests
    render_api_test.cpp
)

target_link_libraries(
    engine_sim_offline_render_tests
    PRIVATE
        engine_sim_offline::render
        engine_sim_offline_p18_reference_catalog
)

add_test(
    NAME render.api
    COMMAND engine_sim_offline_render_tests
)

add_executable(
    engine_sim_offline_schedule_tests
    render_schedule_test.cpp
)

target_include_directories(
    engine_sim_offline_schedule_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_schedule_tests
    PRIVATE engine_sim_offline::render
)

add_test(
    NAME render.schedule
    COMMAND engine_sim_offline_schedule_tests
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
    engine_sim_offline_reference_manifest_encoder_tests
    reference_manifest_encoder_test.cpp
)

target_link_libraries(
    engine_sim_offline_reference_manifest_encoder_tests
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline_p18_reference_catalog
)

add_test(
    NAME artifacts.reference_manifest_encoder
    COMMAND engine_sim_offline_reference_manifest_encoder_tests
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
    NAME dsp.p18_primitives
    COMMAND engine_sim_offline_dsp_primitive_tests
)

add_executable(
    engine_sim_offline_p18_random_table_tests
    p18_random_table_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_random_table_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_random_table_tests
    PRIVATE engine_sim_offline::dsp
)

add_test(
    NAME dsp.p18_random_table
    COMMAND engine_sim_offline_p18_random_table_tests
)

add_executable(
    engine_sim_offline_p18_reconstruction_tests
    p18_causal_reconstruction_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_reconstruction_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_reconstruction_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.p18_reconstruction
    COMMAND engine_sim_offline_p18_reconstruction_tests
)

add_executable(
    engine_sim_offline_p18_conditioning_tests
    p18_conditioning_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_conditioning_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_conditioning_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.p18_conditioning
    COMMAND engine_sim_offline_p18_conditioning_tests
)

add_executable(
    engine_sim_offline_p18_source_stage_tests
    p18_source_stage_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_source_stage_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_source_stage_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.p18_source_stage
    COMMAND engine_sim_offline_p18_source_stage_tests
)

add_executable(
    engine_sim_offline_p18_pcm16_ir_decoder_tests
    p18_pcm16_ir_decoder_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_pcm16_ir_decoder_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_pcm16_ir_decoder_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.p18_pcm16_ir_decoder
    COMMAND engine_sim_offline_p18_pcm16_ir_decoder_tests
)

add_executable(
    engine_sim_offline_p18_static_ir_conversion_tests
    p18_static_ir_conversion_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_static_ir_conversion_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_static_ir_conversion_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.p18_static_ir_conversion
    COMMAND
        engine_sim_offline_p18_static_ir_conversion_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav
)

add_executable(
    engine_sim_offline_p18_fixed_fft_convolution_tests
    p18_fixed_fft_convolution_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_fixed_fft_convolution_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_fixed_fft_convolution_tests
    PRIVATE engine_sim_offline::presentation
)

add_test(
    NAME presentation.p18_fixed_fft_convolution
    COMMAND engine_sim_offline_p18_fixed_fft_convolution_tests
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
        engine_sim_offline::profiles
        engine_sim_offline::simulation
)

add_test(
    NAME simulation.legacy_low_order_gas
    COMMAND engine_sim_offline_legacy_low_order_gas_tests
)

add_executable(
    engine_sim_offline_legacy_low_order_capture_tests
    legacy_low_order_capture_test.cpp
)

target_include_directories(
    engine_sim_offline_legacy_low_order_capture_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_legacy_low_order_capture_tests
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline::profiles
        engine_sim_offline::simulation
)

add_test(
    NAME simulation.legacy_low_order_capture
    COMMAND engine_sim_offline_legacy_low_order_capture_tests
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
        engine_sim_offline::profiles
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
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/reference-parity.bin
)

add_executable(
    engine_sim_offline_bmw_m52b28_parity_request_tests
    bmw_m52b28_parity_request_test.cpp
)

target_include_directories(
    engine_sim_offline_bmw_m52b28_parity_request_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_bmw_m52b28_parity_request_tests
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline::profiles
        engine_sim_offline_reference_parity_v1_reader
        engine_sim_offline_p18_reference_fixture
)

add_test(
    NAME profiles.bmw_m52b28_parity_request
    COMMAND
        engine_sim_offline_bmw_m52b28_parity_request_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/reference-parity.bin
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/component-seeds.bin
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
    engine_sim_offline_p18_reference_provenance_tests
    p18_reference_provenance_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_reference_provenance_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_reference_provenance_tests
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline::presentation
        engine_sim_offline_p18_reference_catalog
        engine_sim_offline_p18_reference_fixture
        engine_sim_offline_p18_reference_provenance
)
engine_sim_offline_enable_renderer_numeric_policy(
    engine_sim_offline_p18_reference_provenance_tests
)

add_test(
    NAME reference.p18_provenance
    COMMAND
        engine_sim_offline_p18_reference_provenance_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18
)

add_executable(
    engine_sim_offline_p18_mastering_tests
    p18_mastering_test.cpp
)

target_include_directories(
    engine_sim_offline_p18_mastering_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_mastering_tests
    PRIVATE
        engine_sim_offline::artifacts
        engine_sim_offline::presentation
)

add_test(
    NAME presentation.p18_mastering
    COMMAND
        engine_sim_offline_p18_mastering_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/presentation/stems/exhaust-0-linear-wet-dry.wav
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/presentation/stems/exhaust-1-linear-wet-dry.wav
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
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

    add_executable(
        engine_sim_offline_p18_presentation_session_boundary_tests
        p18_presentation_session_boundary_test.cpp
    )

    target_include_directories(
        engine_sim_offline_p18_presentation_session_boundary_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )

    target_link_libraries(
        engine_sim_offline_p18_presentation_session_boundary_tests
        PRIVATE
            engine_sim_offline_p18_presentation_session
            $<$<PLATFORM_ID:Linux>:m>
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_p18_presentation_session_boundary_tests
    )

    add_test(
        NAME reference.p18_session_boundary
        COMMAND engine_sim_offline_p18_presentation_session_boundary_tests
    )

    add_executable(
        engine_sim_offline_p18_reference_transaction_tests
        p18_reference_transaction_test.cpp
    )

    target_include_directories(
        engine_sim_offline_p18_reference_transaction_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )

    target_link_options(
        engine_sim_offline_p18_reference_transaction_tests
        PRIVATE $<$<PLATFORM_ID:Linux>:LINKER:--no-as-needed>
    )

    target_link_libraries(
        engine_sim_offline_p18_reference_transaction_tests
        PRIVATE
            engine_sim_offline::artifacts
            engine_sim_offline_p18_presentation_session
            engine_sim_offline_p18_reference_catalog
            engine_sim_offline_p18_reference_fixture
            engine_sim_offline_p18_reference_manifest
            engine_sim_offline_p18_reference_session_adapter
            engine_sim_offline_renderer_determinism
            $<$<PLATFORM_ID:Linux>:m>
    )
    engine_sim_offline_enable_renderer_numeric_policy(
        engine_sim_offline_p18_reference_transaction_tests
    )

    add_test(
        NAME reference.p18_transactional_exact
        COMMAND
            engine_sim_offline_p18_reference_transaction_tests
            ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18
    )
    set_tests_properties(
        reference.p18_transactional_exact
        PROPERTIES SKIP_RETURN_CODE 77
    )

endif()

if(TARGET engine_sim_offline_cli_shell)
    add_executable(
        engine_sim_offline_cli_tests
        cli_shell_test.cpp
    )

    target_include_directories(
        engine_sim_offline_cli_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src/cli
    )

    target_link_libraries(
        engine_sim_offline_cli_tests
        PRIVATE engine_sim_offline_cli_shell
    )

    add_test(
        NAME cli.shell
        COMMAND engine_sim_offline_cli_tests
    )

    add_test(
        NAME cli.process
        COMMAND
            ${CMAKE_COMMAND}
            -DCLI_EXECUTABLE=$<TARGET_FILE:engine_sim_offline_cli>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_process_test.cmake
    )
endif()
