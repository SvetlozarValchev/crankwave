if(NOT DEFINED CLI_EXECUTABLE)
    message(FATAL_ERROR "CLI_EXECUTABLE is required")
endif()
if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

function(run_cli result_var stdout_var stderr_var)
    execute_process(
        COMMAND "${CLI_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_out
        ERROR_VARIABLE standard_error
    )
    set(${result_var} "${result}" PARENT_SCOPE)
    set(${stdout_var} "${standard_out}" PARENT_SCOPE)
    set(${stderr_var} "${standard_error}" PARENT_SCOPE)
endfunction()

run_cli(result standard_out standard_error --help)
if(NOT result STREQUAL "0" OR
   NOT standard_out MATCHES "Usage:" OR
   NOT standard_out MATCHES
       "engine-sim-offline render --engine <engine.json> --scenario <scenario.json>" OR
   NOT standard_out MATCHES
       "--asset-root <directory> --output-directory <new-directory>" OR
   NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "--help process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

run_cli(result standard_out standard_error --version)
if(NOT result STREQUAL "0" OR
   NOT standard_out STREQUAL "engine-sim-offline development\n" OR
   NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "--version process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

run_cli(result standard_out standard_error)
if(NOT result STREQUAL "64" OR
   NOT standard_out STREQUAL "" OR
   NOT standard_error MATCHES "^error:" OR
   NOT standard_error MATCHES
       "Try 'engine-sim-offline --help' for usage\\.")
    message(FATAL_ERROR
        "missing-command process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

run_cli(
    result
    standard_out
    standard_error
    render
    --profile bmw
)
if(NOT result STREQUAL "64" OR
   NOT standard_out STREQUAL "" OR
   NOT standard_error MATCHES "^error:" OR
   NOT standard_error MATCHES
       "Try 'engine-sim-offline --help' for usage\\.")
    message(FATAL_ERROR
        "obsolete render syntax was not rejected as usage\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

get_filename_component(cli_directory "${CLI_EXECUTABLE}" DIRECTORY)
set(missing_engine "${CLI_EXECUTABLE}.definitely-missing-engine.json")
set(missing_scenario "${CLI_EXECUTABLE}.definitely-missing-scenario.json")
set(unused_output "${cli_directory}/cli-process-missing-input-output")
if(EXISTS "${missing_engine}" OR EXISTS "${missing_scenario}")
    message(FATAL_ERROR "nonexistent-input fixture unexpectedly exists")
endif()
file(REMOVE_RECURSE "${unused_output}")

run_cli(
    result
    standard_out
    standard_error
    render
    --engine "${missing_engine}"
    --scenario "${missing_scenario}"
    --asset-root "${cli_directory}"
    --output-directory "${unused_output}"
)
if(NOT result STREQUAL "66" OR
   NOT standard_out STREQUAL "" OR
   NOT standard_error MATCHES "^error:" OR
   EXISTS "${unused_output}")
    message(FATAL_ERROR
        "nonexistent-input process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

set(engine_json "${SOURCE_ROOT}/data/engines/bmw-m52b28/engine.json")
set(canonical_scenario
    "${SOURCE_ROOT}/data/engines/bmw-m52b28/scenarios/inertial-dyno-1500-6500rpm.json")
set(unsupported_scenario
    "${cli_directory}/cli-process-unsupported-scenario.json")
file(READ "${canonical_scenario}" unsupported_scenario_text)
string(REPLACE
    "\"crank_angle\": {\"value\": 2.0943951023933334, \"unit\": \"rad\"}"
    "\"crank_angle\": {\"value\": 0, \"unit\": \"rad\"}"
    unsupported_scenario_text
    "${unsupported_scenario_text}")
if(unsupported_scenario_text MATCHES "2\\.0943951023933334")
    message(FATAL_ERROR "unsupported-capability fixture mutation failed")
endif()
file(WRITE "${unsupported_scenario}" "${unsupported_scenario_text}")

run_cli(
    result
    standard_out
    standard_error
    render
    --engine "${engine_json}"
    --scenario "${unsupported_scenario}"
    --asset-root "${SOURCE_ROOT}"
    --output-directory "${unused_output}"
)
file(REMOVE "${unsupported_scenario}")
if(NOT result STREQUAL "69" OR
   NOT standard_out STREQUAL "" OR
   NOT standard_error MATCHES "unsupported_capability" OR
   EXISTS "${unused_output}")
    message(FATAL_ERROR
        "unsupported-capability exit mapping failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
