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

get_filename_component(cli_directory "${CLI_EXECUTABLE}" DIRECTORY)

run_cli(result standard_out standard_error --help)
if(NOT result STREQUAL "0" OR
   NOT standard_out MATCHES "Usage:" OR
   NOT standard_out MATCHES
       "engine-sim-offline render --engine <engine.json> --scenario <scenario.json>" OR
   NOT standard_out MATCHES
       "--output-directory <new-directory> \\[--asset-root <developer-directory>\\]" OR
   NOT standard_out MATCHES
       "bundled content-addressed asset catalog" OR
   NOT standard_out MATCHES
       "pack-revengine --package-directory <directory>" OR
   NOT standard_out MATCHES
       "verify-revengine --input <file.revengine>" OR
   NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "--help process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

set(revengine_package "${cli_directory}/cli-process-revengine-package")
set(revengine_output "${cli_directory}/cli-process.revengine")
set(revengine_corrupt "${cli_directory}/cli-process-corrupt.revengine")
file(REMOVE_RECURSE "${revengine_package}")
file(REMOVE "${revengine_output}" "${revengine_corrupt}")
file(MAKE_DIRECTORY "${revengine_package}/audio")
file(WRITE "${revengine_package}/runtime.json"
    "{\"schema\":\"engine-sim-offline/responsive-audio-preview\"}\n")
file(WRITE "${revengine_package}/audio/idle.pcm" "deterministic-audio-fixture")
file(SHA256 "${revengine_package}/runtime.json" runtime_manifest_sha256)
file(WRITE "${revengine_package}/revengine.json"
    "{\"schema\":\"engine-sim-offline/revengine-package\","
    "\"version\":1,\"engine_id\":\"cli-process-engine\","
    "\"runtime\":{\"kind\":\"responsive-audio\","
    "\"manifest_path\":\"runtime.json\","
    "\"manifest_sha256\":\"${runtime_manifest_sha256}\"}}\n")

run_cli(
    result standard_out standard_error
    pack-revengine
    --package-directory "${revengine_package}"
    --output "${revengine_output}"
)
if(NOT result STREQUAL "0" OR
   NOT standard_out MATCHES "entry_count=3" OR
   NOT standard_out MATCHES "container_sha256=[0-9a-f]+" OR
   NOT standard_error STREQUAL "" OR
   NOT EXISTS "${revengine_output}")
    message(FATAL_ERROR
        "pack-revengine process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

run_cli(
    result standard_out standard_error
    inspect-revengine --input "${revengine_output}"
)
if(NOT result STREQUAL "0" OR
   NOT standard_out MATCHES "revengine_version=1" OR
   NOT standard_out MATCHES "verified=false" OR
   NOT standard_out MATCHES "entry=revengine.json" OR
   standard_out MATCHES "engine_id=" OR
   NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "inspect-revengine process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

run_cli(
    result standard_out standard_error
    verify-revengine --input "${revengine_output}"
)
if(NOT result STREQUAL "0" OR
   NOT standard_out MATCHES "verified=true" OR
   NOT standard_out MATCHES "engine_id=cli-process-engine" OR
   NOT standard_out MATCHES "runtime_kind=responsive-audio" OR
   NOT standard_out MATCHES "runtime_manifest_path=runtime.json" OR
   NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "verify-revengine process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

run_cli(
    result standard_out standard_error
    pack-revengine
    --package-directory "${revengine_package}"
    --output "${revengine_output}"
)
if(NOT result STREQUAL "73" OR
   NOT standard_out STREQUAL "" OR
   NOT standard_error MATCHES "already exists")
    message(FATAL_ERROR
        "pack-revengine overwrite rejection failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

file(COPY_FILE "${revengine_output}" "${revengine_corrupt}")
file(APPEND "${revengine_corrupt}" "corrupt")
run_cli(
    result standard_out standard_error
    verify-revengine --input "${revengine_corrupt}"
)
if(NOT result STREQUAL "65" OR
   NOT standard_out STREQUAL "" OR
   NOT standard_error MATCHES "invalid REVENGINE container")
    message(FATAL_ERROR
        "verify-revengine corruption rejection failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

file(REMOVE_RECURSE "${revengine_package}")
file(REMOVE "${revengine_output}" "${revengine_corrupt}")

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
