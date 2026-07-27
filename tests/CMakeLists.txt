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
    PRIVATE engine_sim_offline::contract
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
    PRIVATE engine_sim_offline::render
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
    ${PROJECT_SOURCE_DIR}/src/reference/p18_reference_audit_reader.cpp
)

target_include_directories(
    engine_sim_offline_p18_reference_audit_reader_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_reference_audit_reader_tests
    PRIVATE engine_sim_offline::build_options
)

add_test(
    NAME reference.p18_audit_reader
    COMMAND
        engine_sim_offline_p18_reference_audit_reader_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/reference-audit.bin
)

add_executable(
    engine_sim_offline_p18_reference_seed_reader_tests
    p18_reference_seed_reader_test.cpp
    ${PROJECT_SOURCE_DIR}/src/reference/p18_reference_seed_reader.cpp
)

target_include_directories(
    engine_sim_offline_p18_reference_seed_reader_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
    engine_sim_offline_p18_reference_seed_reader_tests
    PRIVATE engine_sim_offline::build_options
)

add_test(
    NAME reference.p18_seed_reader
    COMMAND
        engine_sim_offline_p18_reference_seed_reader_tests
        ${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/component-seeds.bin
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
