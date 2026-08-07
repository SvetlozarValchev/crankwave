if(NOT DEFINED CLI_EXECUTABLE)
    message(FATAL_ERROR "CLI_EXECUTABLE is required")
endif()
if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED RELEASE_IDENTITY OR RELEASE_IDENTITY STREQUAL "")
    message(FATAL_ERROR "RELEASE_IDENTITY is required")
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
set(revengine_machine_output "${cli_directory}/cli-process-machine.revengine")
set(revengine_corrupt "${cli_directory}/cli-process-corrupt.revengine")
set(revengine_deadline_output "${cli_directory}/cli-process-deadline.revengine")
file(REMOVE_RECURSE "${revengine_package}")
file(REMOVE
    "${revengine_output}"
    "${revengine_machine_output}"
    "${revengine_corrupt}"
    "${revengine_deadline_output}")
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
    pack-revengine
    --package-directory "${revengine_package}"
    --output "${revengine_machine_output}"
    --result-format json
)
if(NOT result STREQUAL "0" OR
   NOT standard_error STREQUAL "" OR
   NOT EXISTS "${revengine_machine_output}")
    message(FATAL_ERROR
        "machine pack-revengine process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
string(JSON machine_schema GET "${standard_out}" schema)
string(JSON machine_release GET "${standard_out}" release_identity)
string(JSON machine_command GET "${standard_out}" command)
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
string(JSON machine_exit GET "${standard_out}" exit_code)
string(JSON machine_entry_count GET "${standard_out}" result entry_count)
string(JSON machine_output_file GET "${standard_out}" result output_file)
if(NOT machine_schema STREQUAL "engine-sim-offline.cli-result.v1" OR
   NOT machine_release STREQUAL RELEASE_IDENTITY OR
   NOT machine_command STREQUAL "pack-revengine" OR
   NOT machine_ok OR
   NOT machine_code STREQUAL "success" OR
   NOT machine_exit EQUAL 0 OR
   NOT machine_entry_count EQUAL 3 OR
   NOT machine_output_file STREQUAL revengine_machine_output)
    message(FATAL_ERROR "machine pack-revengine JSON fields are invalid")
endif()

run_cli(
    result standard_out standard_error
    pack-revengine
    --package-directory "${revengine_package}"
    --output "${revengine_machine_output}"
    --result-format json
)
if(NOT result STREQUAL "73" OR NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "machine pack overwrite rejection failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
string(JSON machine_command GET "${standard_out}" command)
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
if(NOT machine_command STREQUAL "pack-revengine" OR
   machine_ok OR
   NOT machine_code STREQUAL "revengine-output-unavailable")
    message(FATAL_ERROR "machine pack failure JSON fields are invalid")
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
    inspect-revengine
    --input "${revengine_machine_output}"
    --result-format json
)
if(NOT result STREQUAL "0" OR NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "machine inspect-revengine process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
string(JSON machine_command GET "${standard_out}" command)
string(JSON machine_verified GET "${standard_out}" result verified)
string(JSON machine_package_type TYPE "${standard_out}" result package)
if(NOT machine_command STREQUAL "inspect-revengine" OR
   machine_verified OR
   NOT machine_package_type STREQUAL "NULL")
    message(FATAL_ERROR "machine inspect-revengine JSON fields are invalid")
endif()

run_cli(
    result standard_out standard_error
    inspect-revengine
    --input "${revengine_machine_output}.missing"
    --result-format json
)
if(NOT result STREQUAL "66" OR NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "machine inspect missing-input rejection failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
string(JSON machine_command GET "${standard_out}" command)
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
if(NOT machine_command STREQUAL "inspect-revengine" OR
   machine_ok OR
   NOT machine_code STREQUAL "revengine-input-unavailable")
    message(FATAL_ERROR "machine inspect failure JSON fields are invalid")
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
    verify-revengine
    --input "${revengine_machine_output}"
    --result-format json
)
if(NOT result STREQUAL "0" OR NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "machine verify-revengine process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
string(JSON machine_command GET "${standard_out}" command)
string(JSON machine_verified GET "${standard_out}" result verified)
string(JSON machine_engine_id GET "${standard_out}" result package engine_id)
if(NOT machine_command STREQUAL "verify-revengine" OR
   NOT machine_verified OR
   NOT machine_engine_id STREQUAL "cli-process-engine")
    message(FATAL_ERROR "machine verify-revengine JSON fields are invalid")
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

run_cli(
    result standard_out standard_error
    verify-revengine
    --input "${revengine_corrupt}"
    --result-format json
)
if(NOT result STREQUAL "65" OR
   NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "machine verify-revengine corruption rejection failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
string(JSON machine_exit GET "${standard_out}" exit_code)
if(machine_ok OR
   NOT machine_code STREQUAL "revengine-data-error" OR
   NOT machine_exit EQUAL 65)
    message(FATAL_ERROR "machine verify failure JSON fields are invalid")
endif()

run_cli(
    result standard_out standard_error
    pack-revengine
    --package-directory "${revengine_package}"
    --output "${revengine_deadline_output}"
    --deadline-unix-ms 1
    --result-format json
)
if(NOT result STREQUAL "75" OR
   NOT standard_error STREQUAL "" OR
   EXISTS "${revengine_deadline_output}")
    message(FATAL_ERROR
        "expired pack deadline process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
string(JSON machine_exit GET "${standard_out}" exit_code)
if(machine_ok OR
   NOT machine_code STREQUAL "pack-revengine-deadline-exceeded" OR
   NOT machine_exit EQUAL 75)
    message(FATAL_ERROR "expired pack deadline JSON fields are invalid")
endif()

run_cli(
    result standard_out standard_error
    verify-revengine
    --input "${revengine_machine_output}"
    --deadline-unix-ms 1
    --result-format json
)
if(NOT result STREQUAL "75" OR NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "expired verify deadline process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
string(JSON machine_exit GET "${standard_out}" exit_code)
if(machine_ok OR
   NOT machine_code STREQUAL "verify-revengine-deadline-exceeded" OR
   NOT machine_exit EQUAL 75)
    message(FATAL_ERROR "expired verify deadline JSON fields are invalid")
endif()

set(pack_signal_root "${cli_directory}/cli-process-pack-signal-root")
set(pack_signal_output "${pack_signal_root}/cancelled.revengine")
set(pack_signal_stdout "${pack_signal_root}/signal.stdout")
set(pack_signal_stderr "${pack_signal_root}/signal.stderr")
set(pack_signal_padding "${revengine_package}/audio/cancellation-padding.pcm")
file(REMOVE_RECURSE "${pack_signal_root}")
file(MAKE_DIRECTORY "${pack_signal_root}")
find_program(TRUNCATE_EXECUTABLE NAMES truncate REQUIRED)
execute_process(
    COMMAND "${TRUNCATE_EXECUTABLE}" -s 67108864 "${pack_signal_padding}"
    RESULT_VARIABLE truncate_result
)
if(NOT truncate_result STREQUAL "0")
    message(FATAL_ERROR "pack signal fixture could not be created")
endif()
find_program(BASH_EXECUTABLE NAMES bash REQUIRED)
execute_process(
    COMMAND
        "${BASH_EXECUTABLE}"
        "${SOURCE_ROOT}/tests/cli_pack_signal_process_test.sh"
        "${CLI_EXECUTABLE}"
        "${revengine_package}"
        "${pack_signal_output}"
        "${pack_signal_stdout}"
        "${pack_signal_stderr}"
        "${pack_signal_root}"
        "TERM"
    RESULT_VARIABLE pack_signal_result
    OUTPUT_VARIABLE pack_signal_helper_stdout
    ERROR_VARIABLE pack_signal_helper_stderr
)
file(READ "${pack_signal_stdout}" standard_out)
file(READ "${pack_signal_stderr}" standard_error)
file(GLOB pack_signal_stages
    "${pack_signal_root}/.engine-sim-offline-stage-*")
if(NOT pack_signal_result STREQUAL "75" OR
   NOT pack_signal_helper_stdout STREQUAL "" OR
   NOT pack_signal_helper_stderr STREQUAL "" OR
   NOT standard_error STREQUAL "" OR
   EXISTS "${pack_signal_output}" OR
   pack_signal_stages)
    message(FATAL_ERROR
        "SIGTERM pack process contract failed\n"
        "exit: ${pack_signal_result}\nstdout: ${standard_out}\n"
        "stderr: ${standard_error}\nhelper stderr: ${pack_signal_helper_stderr}\n"
        "remaining stages: ${pack_signal_stages}")
endif()
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
string(JSON machine_exit GET "${standard_out}" exit_code)
if(machine_ok OR
   NOT machine_code STREQUAL "pack-revengine-terminated" OR
   NOT machine_exit EQUAL 75)
    message(FATAL_ERROR "SIGTERM pack JSON fields are invalid")
endif()

set(verify_signal_input "${pack_signal_root}/verify-input.revengine")
run_cli(
    result standard_out standard_error
    pack-revengine
    --package-directory "${revengine_package}"
    --output "${verify_signal_input}"
)
if(NOT result STREQUAL "0" OR
   NOT standard_error STREQUAL "" OR
   NOT EXISTS "${verify_signal_input}")
    message(FATAL_ERROR
        "verify signal fixture could not be packed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
file(REMOVE "${pack_signal_stdout}" "${pack_signal_stderr}")
execute_process(
    COMMAND
        "${BASH_EXECUTABLE}"
        "${SOURCE_ROOT}/tests/cli_verify_signal_process_test.sh"
        "${CLI_EXECUTABLE}"
        "${verify_signal_input}"
        "${pack_signal_stdout}"
        "${pack_signal_stderr}"
        "TERM"
    RESULT_VARIABLE verify_signal_result
    OUTPUT_VARIABLE verify_signal_helper_stdout
    ERROR_VARIABLE verify_signal_helper_stderr
)
file(READ "${pack_signal_stdout}" standard_out)
file(READ "${pack_signal_stderr}" standard_error)
if(NOT verify_signal_result STREQUAL "75" OR
   NOT verify_signal_helper_stdout STREQUAL "" OR
   NOT verify_signal_helper_stderr STREQUAL "" OR
   NOT standard_error STREQUAL "")
    message(FATAL_ERROR
        "SIGTERM verify process contract failed\n"
        "exit: ${verify_signal_result}\nstdout: ${standard_out}\n"
        "stderr: ${standard_error}\nhelper stderr: ${verify_signal_helper_stderr}")
endif()
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
string(JSON machine_exit GET "${standard_out}" exit_code)
if(machine_ok OR
   NOT machine_code STREQUAL "verify-revengine-terminated" OR
   NOT machine_exit EQUAL 75)
    message(FATAL_ERROR "SIGTERM verify JSON fields are invalid")
endif()
file(REMOVE "${pack_signal_padding}")
file(REMOVE_RECURSE "${pack_signal_root}")

file(REMOVE_RECURSE "${revengine_package}")
file(REMOVE
    "${revengine_output}"
    "${revengine_machine_output}"
    "${revengine_corrupt}"
    "${revengine_deadline_output}")

run_cli(result standard_out standard_error --version)
if(NOT result STREQUAL "0" OR
   NOT standard_out STREQUAL
       "engine-sim-offline ${RELEASE_IDENTITY}\n" OR
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
set(signal_engine "${cli_directory}/cli-process-signal-engine.json")
set(signal_stdout "${cli_directory}/cli-process-signal.stdout")
set(signal_stderr "${cli_directory}/cli-process-signal.stderr")
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

# Keep the child in a cancellable input stage long enough to deliver a real process
# signal without depending on renderer speed. JSON permits trailing spaces, and the
# padded document remains beneath the native 8 MiB document limit.
file(READ "${engine_json}" signal_engine_text)
string(REPEAT " " 3500000 signal_engine_padding)
file(WRITE "${signal_engine}"
    "${signal_engine_text}${signal_engine_padding}")
unset(signal_engine_text)
unset(signal_engine_padding)
file(REMOVE "${signal_stdout}" "${signal_stderr}")
find_program(BASH_EXECUTABLE NAMES bash REQUIRED)
execute_process(
    COMMAND
        "${BASH_EXECUTABLE}"
        "${SOURCE_ROOT}/tests/cli_signal_process_test.sh"
        "${CLI_EXECUTABLE}"
        "${signal_engine}"
        "${canonical_scenario}"
        "${unused_output}"
        "${signal_stdout}"
        "${signal_stderr}"
        "TERM"
    RESULT_VARIABLE signal_result
    OUTPUT_VARIABLE signal_helper_stdout
    ERROR_VARIABLE signal_helper_stderr
)
file(READ "${signal_stdout}" standard_out)
file(READ "${signal_stderr}" standard_error)
file(REMOVE "${signal_stdout}" "${signal_stderr}")
if(NOT signal_result STREQUAL "75" OR
   NOT signal_helper_stdout STREQUAL "" OR
   NOT signal_helper_stderr STREQUAL "" OR
   NOT standard_error STREQUAL "" OR
   EXISTS "${unused_output}")
    message(FATAL_ERROR
        "SIGTERM render process contract failed\n"
        "exit: ${signal_result}\nstdout: ${standard_out}\n"
        "stderr: ${standard_error}\nhelper stderr: ${signal_helper_stderr}")
endif()
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
string(JSON machine_exit GET "${standard_out}" exit_code)
if(machine_ok OR
   NOT machine_code STREQUAL "render-terminated" OR
   NOT machine_exit EQUAL 75)
    message(FATAL_ERROR "SIGTERM render JSON fields are invalid")
endif()

execute_process(
    COMMAND
        "${BASH_EXECUTABLE}"
        "${SOURCE_ROOT}/tests/cli_signal_process_test.sh"
        "${CLI_EXECUTABLE}"
        "${signal_engine}"
        "${canonical_scenario}"
        "${unused_output}"
        "${signal_stdout}"
        "${signal_stderr}"
        "INT"
    RESULT_VARIABLE signal_result
    OUTPUT_VARIABLE signal_helper_stdout
    ERROR_VARIABLE signal_helper_stderr
)
file(READ "${signal_stdout}" standard_out)
file(READ "${signal_stderr}" standard_error)
file(REMOVE "${signal_engine}" "${signal_stdout}" "${signal_stderr}")
if(NOT signal_result STREQUAL "75" OR
   NOT signal_helper_stdout STREQUAL "" OR
   NOT signal_helper_stderr STREQUAL "" OR
   NOT standard_error STREQUAL "" OR
   EXISTS "${unused_output}")
    message(FATAL_ERROR
        "SIGINT render process contract failed\n"
        "exit: ${signal_result}\nstdout: ${standard_out}\n"
        "stderr: ${standard_error}\nhelper stderr: ${signal_helper_stderr}")
endif()
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
string(JSON machine_exit GET "${standard_out}" exit_code)
if(machine_ok OR
   NOT machine_code STREQUAL "render-terminated" OR
   NOT machine_exit EQUAL 75)
    message(FATAL_ERROR "SIGINT render JSON fields are invalid")
endif()

run_cli(
    result
    standard_out
    standard_error
    render
    --engine "${engine_json}"
    --scenario "${canonical_scenario}"
    --output-directory "${unused_output}"
    --deadline-unix-ms 1
    --result-format json
)
if(NOT result STREQUAL "75" OR
   NOT standard_error STREQUAL "" OR
   EXISTS "${unused_output}")
    message(FATAL_ERROR
        "expired render deadline process contract failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()
string(JSON machine_ok GET "${standard_out}" ok)
string(JSON machine_code GET "${standard_out}" code)
string(JSON machine_exit GET "${standard_out}" exit_code)
if(machine_ok OR
   NOT machine_code STREQUAL "render-deadline-exceeded" OR
   NOT machine_exit EQUAL 75)
    message(FATAL_ERROR "expired render deadline JSON fields are invalid")
endif()

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
