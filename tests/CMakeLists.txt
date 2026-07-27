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
