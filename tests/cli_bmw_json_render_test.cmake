foreach(required_variable CLI_EXECUTABLE SOURCE_ROOT OUTPUT_ROOT)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "the native publication integration test requires Linux")
endif()

get_filename_component(source_root "${SOURCE_ROOT}" REALPATH)
get_filename_component(output_root "${OUTPUT_ROOT}" ABSOLUTE)
file(MAKE_DIRECTORY "${output_root}")
get_filename_component(output_root "${output_root}" REALPATH)

if(output_root STREQUAL "/")
    message(FATAL_ERROR "OUTPUT_ROOT must not resolve to the filesystem root")
endif()

set(engine_package "${source_root}/data/engines/bmw-m52b28")
set(engine_json "${engine_package}/engine.json")
set(scenario_json
    "${engine_package}/scenarios/inertial-dyno-1500-6500rpm.json")
set(publication_directory "${output_root}/cli-bmw-json-render")
set(audition_wave
    "${publication_directory}/audio/master.engine.audition.wav")
set(manifest
    "${publication_directory}/manifest/render-manifest.v6.json")
set(manifest_sidecar "${manifest}.sha256")

if(NOT EXISTS "${CLI_EXECUTABLE}" OR
   NOT EXISTS "${engine_json}" OR
   NOT EXISTS "${scenario_json}")
    message(FATAL_ERROR "CLI or authored BMW JSON fixture is unavailable")
endif()

# This is the test's one exact binary-tree publication destination. Remove only this
# destination so a previous interrupted invocation cannot turn the no-overwrite
# publication rule into a false failure.
file(REMOVE_RECURSE "${publication_directory}")

execute_process(
    COMMAND
        "${CLI_EXECUTABLE}"
        render
        --engine "${engine_json}"
        --scenario "${scenario_json}"
        --asset-root "${source_root}"
        --output-directory "${publication_directory}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_out
    ERROR_VARIABLE standard_error
    TIMEOUT 300
)

string(CONCAT expected_stdout
    "output_directory=${publication_directory}\n"
    "manifest=${manifest}\n")
if(NOT result STREQUAL "0" OR
   NOT standard_error STREQUAL "" OR
   NOT standard_out STREQUAL expected_stdout)
    message(FATAL_ERROR
        "BMW JSON CLI render failed\n"
        "exit: ${result}\nstdout: ${standard_out}\nstderr: ${standard_error}")
endif()

if(NOT EXISTS "${audition_wave}")
    message(FATAL_ERROR "CLI render did not publish the audition WAV")
endif()
file(SIZE "${audition_wave}" audition_wave_size)
file(SHA256 "${audition_wave}" audition_wave_sha256)
if(NOT audition_wave_size EQUAL 8640586 OR
   NOT audition_wave_sha256 STREQUAL
       "f603ffed10dfe95b895084140cac46c448cafc4127c96b1671e53575b47ae552")
    message(FATAL_ERROR
        "CLI audition WAV identity changed\n"
        "size: ${audition_wave_size}\nsha256: ${audition_wave_sha256}")
endif()

if(NOT EXISTS "${manifest}" OR NOT EXISTS "${manifest_sidecar}")
    message(FATAL_ERROR "CLI render did not publish manifest evidence")
endif()
file(SHA256 "${manifest}" manifest_sha256)
file(READ "${manifest_sidecar}" sidecar_sha256)
string(STRIP "${sidecar_sha256}" sidecar_sha256)
if(NOT sidecar_sha256 STREQUAL manifest_sha256)
    message(FATAL_ERROR
        "manifest SHA-256 sidecar disagrees with the manifest\n"
        "manifest: ${manifest_sha256}\nsidecar: ${sidecar_sha256}")
endif()

file(READ "${manifest}" manifest_text)
foreach(required_manifest_fragment
        "\"wire_schema\":\"engine-sim-offline.render-manifest.simulation.v6\""
        "\"schema_version\":6"
        "\"engine_id\":{\"value\":\"bmw-m52b28\""
        "\"scenario_id\":\"bmw-m52b28-inertial-dyno-1500-6500rpm\""
        "\"source_matrix_id\":\"scenario-source-matrix.bmw-m52b28-inertial-dyno-1500-6500rpm\"")
    string(FIND
        "${manifest_text}"
        "${required_manifest_fragment}"
        fragment_offset)
    if(fragment_offset EQUAL -1)
        message(FATAL_ERROR
            "manifest omitted generic identity/schema fragment: "
            "${required_manifest_fragment}")
    endif()
endforeach()

# Preserve failed output for diagnosis, but leave a successful binary tree clean.
file(REMOVE_RECURSE "${publication_directory}")
