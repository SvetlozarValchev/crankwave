foreach(required IN ITEMS
        BUILD_DIRECTORY
        INSTALL_PREFIX
        INSTALL_BINDIR
        INSTALL_DATADIR
        INSTALL_LIBEXECDIR
        NODE_EXECUTABLE
        SOURCE_ROOT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

foreach(relative_directory IN ITEMS
        INSTALL_BINDIR
        INSTALL_DATADIR
        INSTALL_LIBEXECDIR)
    if(IS_ABSOLUTE "${${relative_directory}}")
        message(FATAL_ERROR
            "installed-distribution smoke requires relative GNUInstallDirs paths")
    endif()
endforeach()

set(staged_install_prefix "${INSTALL_PREFIX}.before-relocation")
file(REMOVE_RECURSE "${staged_install_prefix}" "${INSTALL_PREFIX}")
set(install_command
    "${CMAKE_COMMAND}" --install "${BUILD_DIRECTORY}"
    --prefix "${staged_install_prefix}")
if(DEFINED INSTALL_CONFIG AND NOT INSTALL_CONFIG STREQUAL "")
    list(APPEND install_command --config "${INSTALL_CONFIG}")
endif()
execute_process(
    COMMAND ${install_command}
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_stderr
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR
        "prefix install failed\n"
        "exit: ${install_result}\nstdout: ${install_stdout}\nstderr: ${install_stderr}")
endif()
file(RENAME "${staged_install_prefix}" "${INSTALL_PREFIX}")

set(bin_directory "${INSTALL_PREFIX}/${INSTALL_BINDIR}")
set(resource_root
    "${INSTALL_PREFIX}/${INSTALL_DATADIR}/engine-sim-offline")
set(helper
    "${INSTALL_PREFIX}/${INSTALL_LIBEXECDIR}/engine-sim-offline/dump-ir-spectrum")
set(cli "${bin_directory}/engine-sim-offline")
set(launcher "${bin_directory}/engine-sim-offline-responsive-bake")
set(baker_root "${resource_root}/tools/responsive-audio-baker")
set(runtime_root "${resource_root}/web/runtime")

foreach(required_file IN ITEMS
        "${cli}"
        "${launcher}"
        "${helper}"
        "${resource_root}/package.json"
        "${resource_root}/assets/catalog.v1.json"
        "${baker_root}/bake.mjs"
        "${baker_root}/dump-ir-spectrum.cpp"
        "${baker_root}/profiles/interactive-preview-v1.json"
        "${resource_root}/schemas/responsive-audio-bake-profile.schema.json")
    if(NOT EXISTS "${required_file}" OR IS_DIRECTORY "${required_file}")
        message(FATAL_ERROR "installed distribution file is absent: ${required_file}")
    endif()
endforeach()

file(GLOB baker_entries RELATIVE "${baker_root}" "${baker_root}/*")
list(SORT baker_entries)
set(expected_baker_entries
    bake.mjs
    dump-ir-spectrum.cpp
    internal
    profiles)
if(NOT "${baker_entries}" STREQUAL "${expected_baker_entries}")
    message(FATAL_ERROR
        "installed baker resource closure differs: ${baker_entries}")
endif()

file(GLOB internal_entries
    RELATIVE "${baker_root}/internal" "${baker_root}/internal/*")
list(SORT internal_entries)
set(expected_internal_entries
    bake-contract.mjs
    directional-transients.mjs
    held-texture.mjs
    lifecycle.mjs)
if(NOT "${internal_entries}" STREQUAL "${expected_internal_entries}")
    message(FATAL_ERROR
        "installed baker internal-stage closure differs: ${internal_entries}")
endif()

file(GLOB runtime_entries RELATIVE "${runtime_root}" "${runtime_root}/*")
list(SORT runtime_entries)
set(expected_runtime_entries
    c-api-abi.js
    c-api-client.js
    c-api-errors.js
    c-api-session.js
    wasm-heap.js)
if(NOT "${runtime_entries}" STREQUAL "${expected_runtime_entries}")
    message(FATAL_ERROR
        "installed C API JavaScript closure differs: ${runtime_entries}")
endif()

file(READ "${resource_root}/package.json" package_metadata)
string(JSON package_type GET "${package_metadata}" type)
string(JSON node_requirement GET "${package_metadata}" engines node)
if(NOT package_type STREQUAL "module" OR
   NOT node_requirement STREQUAL ">=20.11.0")
    message(FATAL_ERROR "installed Node package metadata is invalid")
endif()

execute_process(
    COMMAND "${helper}"
    RESULT_VARIABLE helper_result
    OUTPUT_VARIABLE helper_stdout
    ERROR_VARIABLE helper_stderr
)
if(NOT helper_result EQUAL 2 OR
   NOT helper_stdout STREQUAL "" OR
   NOT helper_stderr MATCHES "^usage: dump-ir-spectrum")
    message(FATAL_ERROR
        "installed IR helper smoke failed\n"
        "exit: ${helper_result}\nstdout: ${helper_stdout}\nstderr: ${helper_stderr}")
endif()

set(helper_output "${INSTALL_PREFIX}/smooth-39-spectrum.bin")
execute_process(
    COMMAND
        "${helper}"
        "${SOURCE_ROOT}/reference/fixtures/engine-sim-ir-library/presentation/smooth_39.wav"
        1.0
        "${helper_output}"
    RESULT_VARIABLE helper_render_result
    OUTPUT_VARIABLE helper_render_stdout
    ERROR_VARIABLE helper_render_stderr
)
if(EXISTS "${helper_output}")
    file(SIZE "${helper_output}" helper_output_bytes)
else()
    set(helper_output_bytes 0)
endif()
if(NOT helper_render_result EQUAL 0 OR
   NOT helper_render_stdout STREQUAL "" OR
   NOT helper_render_stderr STREQUAL "" OR
   NOT helper_output_bytes EQUAL 1048576)
    message(FATAL_ERROR
        "installed IR helper conversion failed\n"
        "exit: ${helper_render_result}\nbytes: ${helper_output_bytes}\n"
        "stdout: ${helper_render_stdout}\nstderr: ${helper_render_stderr}")
endif()

execute_process(
    COMMAND
        "${NODE_EXECUTABLE}" --input-type=module --eval
        "const {pathToFileURL}=await import('node:url'); await import(pathToFileURL(process.argv[1]).href);"
        "${runtime_root}/c-api-client.js"
    RESULT_VARIABLE javascript_result
    OUTPUT_VARIABLE javascript_stdout
    ERROR_VARIABLE javascript_stderr
)
if(NOT javascript_result EQUAL 0 OR
   NOT javascript_stdout STREQUAL "" OR
   NOT javascript_stderr STREQUAL "")
    message(FATAL_ERROR
        "installed C API JavaScript import failed\n"
        "exit: ${javascript_result}\n"
        "stdout: ${javascript_stdout}\nstderr: ${javascript_stderr}")
endif()

set(plan_output "${INSTALL_PREFIX}/responsive-plan-output")
set(plan_cache "${INSTALL_PREFIX}/responsive-plan-cache")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "ENGINE_SIM_OFFLINE_NODE=${NODE_EXECUTABLE}"
        "${launcher}"
        --engine
        "${SOURCE_ROOT}/data/engines/bmw-m52tub28-cleanroom/engine.json"
        --profile "${baker_root}/profiles/interactive-preview-v1.json"
        --output "${plan_output}"
        --cache "${plan_cache}"
        --plan
    WORKING_DIRECTORY "${INSTALL_PREFIX}"
    RESULT_VARIABLE plan_result
    OUTPUT_VARIABLE plan_stdout
    ERROR_VARIABLE plan_stderr
)
if(NOT plan_result EQUAL 0 OR
   NOT plan_stdout MATCHES
       "\"schema\": \"engine-sim-offline/responsive-audio-bake-plan-v1\"" OR
   NOT plan_stderr STREQUAL "" OR
   EXISTS "${plan_output}" OR EXISTS "${plan_cache}")
    message(FATAL_ERROR
        "installed responsive-baker plan smoke failed\n"
        "exit: ${plan_result}\nstdout: ${plan_stdout}\nstderr: ${plan_stderr}")
endif()

# A distribution configured with an explicit renderer pair must prove the real
# relocated path, not only resource discovery in plan mode. Normal native-only
# builds omit the renderer and deliberately skip this slower gate.
set(installed_renderer_javascript
    "${resource_root}/renderer/engine-sim-offline.js")
set(installed_renderer_wasm
    "${resource_root}/renderer/engine-sim-offline.wasm")
if(EXISTS "${installed_renderer_javascript}" AND
   EXISTS "${installed_renderer_wasm}")
    set(real_bake_output "${INSTALL_PREFIX}/responsive-smoke-output")
    # Keep the caller-owned cache outside the relocatable prefix. Re-running
    # this gate reinstalls/moves the prefix but may validly reuse exact cached
    # captures after all content identities have been checked.
    set(real_bake_cache "${INSTALL_PREFIX}.responsive-smoke-cache")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "ENGINE_SIM_OFFLINE_NODE=${NODE_EXECUTABLE}"
            "${launcher}"
            --engine
            "${SOURCE_ROOT}/data/engines/bmw-m52tub28-cleanroom/engine.json"
            --profile
            "${SOURCE_ROOT}/tools/responsive-audio-baker/testdata/smoke-profile.json"
            --output "${real_bake_output}"
            --cache "${real_bake_cache}"
            --jobs 6
        WORKING_DIRECTORY "${INSTALL_PREFIX}"
        RESULT_VARIABLE real_bake_result
        OUTPUT_VARIABLE real_bake_stdout
        ERROR_VARIABLE real_bake_stderr
        TIMEOUT 90
    )
    if(NOT real_bake_result EQUAL 0 OR
       NOT EXISTS "${real_bake_output}/runtime.json" OR
       NOT EXISTS "${real_bake_output}/revengine.json")
        message(FATAL_ERROR
            "installed responsive-baker real smoke failed\n"
            "exit: ${real_bake_result}\n"
            "stdout: ${real_bake_stdout}\nstderr: ${real_bake_stderr}")
    endif()

    set(real_carrier "${INSTALL_PREFIX}/responsive-smoke.revengine")
    execute_process(
        COMMAND
            "${cli}" pack-revengine
            --package-directory "${real_bake_output}"
            --output "${real_carrier}"
        RESULT_VARIABLE real_pack_result
        OUTPUT_VARIABLE real_pack_stdout
        ERROR_VARIABLE real_pack_stderr
    )
    if(NOT real_pack_result EQUAL 0 OR
       NOT real_pack_stdout MATCHES "(^|\n)output_file=" OR
       NOT real_pack_stdout MATCHES "(^|\n)container_sha256=" OR
       NOT real_pack_stderr STREQUAL "" OR
       NOT EXISTS "${real_carrier}")
        message(FATAL_ERROR
            "installed REVENGINE pack smoke failed\n"
            "exit: ${real_pack_result}\n"
            "stdout: ${real_pack_stdout}\nstderr: ${real_pack_stderr}")
    endif()

    execute_process(
        COMMAND
            "${cli}" verify-revengine --input "${real_carrier}"
        RESULT_VARIABLE real_verify_result
        OUTPUT_VARIABLE real_verify_stdout
        ERROR_VARIABLE real_verify_stderr
    )
    if(NOT real_verify_result EQUAL 0 OR
       NOT real_verify_stdout MATCHES "(^|\n)verified=true(\n|$)" OR
       NOT real_verify_stderr STREQUAL "")
        message(FATAL_ERROR
            "installed REVENGINE verify smoke failed\n"
            "exit: ${real_verify_result}\n"
            "stdout: ${real_verify_stdout}\nstderr: ${real_verify_stderr}")
    endif()

    execute_process(
        COMMAND
            "${NODE_EXECUTABLE}" --input-type=module --eval
            "const {readFile}=await import('node:fs/promises'); const {webcrypto}=await import('node:crypto'); const {pathToFileURL}=await import('node:url'); const module=await import(pathToFileURL(process.argv[1]).href); const loaded=await module.loadResponsiveAudioRevengine(await readFile(process.argv[2]),{crypto:webcrypto}); if(!loaded.runtime.heldPackage||!loaded.runtime.directionalPackage||!loaded.runtime.lifecyclePackage||!loaded.runtime.sharedRecordedStarterPackage) process.exit(2);"
            "${SOURCE_ROOT}/web/runtime/revengine-package.js"
            "${real_carrier}"
        RESULT_VARIABLE real_javascript_result
        OUTPUT_VARIABLE real_javascript_stdout
        ERROR_VARIABLE real_javascript_stderr
        TIMEOUT 30
    )
    if(NOT real_javascript_result EQUAL 0 OR
       NOT real_javascript_stdout STREQUAL "" OR
       NOT real_javascript_stderr STREQUAL "")
        message(FATAL_ERROR
            "installed REVENGINE JavaScript load smoke failed\n"
            "exit: ${real_javascript_result}\n"
            "stdout: ${real_javascript_stdout}\n"
            "stderr: ${real_javascript_stderr}")
    endif()
endif()

set(canonical_scenario
    "${SOURCE_ROOT}/data/engines/bmw-m52b28/scenarios/inertial-dyno-1500-6500rpm.json")
set(unsupported_scenario "${INSTALL_PREFIX}/unsupported-scenario.json")
file(READ "${canonical_scenario}" unsupported_scenario_text)
string(REPLACE
    "\"crank_angle\": {\"value\": 2.0943951023933334, \"unit\": \"rad\"}"
    "\"crank_angle\": {\"value\": 0, \"unit\": \"rad\"}"
    unsupported_scenario_text
    "${unsupported_scenario_text}")
if(unsupported_scenario_text MATCHES "2\\.0943951023933334")
    message(FATAL_ERROR "installed CLI scenario mutation failed")
endif()
file(WRITE "${unsupported_scenario}" "${unsupported_scenario_text}")
set(render_output "${INSTALL_PREFIX}/render-output")
execute_process(
    COMMAND
        "${cli}" render
        --engine "${SOURCE_ROOT}/data/engines/bmw-m52b28/engine.json"
        --scenario "${unsupported_scenario}"
        --output-directory "${render_output}"
    RESULT_VARIABLE cli_result
    OUTPUT_VARIABLE cli_stdout
    ERROR_VARIABLE cli_stderr
)
if(NOT cli_result EQUAL 69 OR
   NOT cli_stdout STREQUAL "" OR
   NOT cli_stderr MATCHES "unsupported_capability" OR
   EXISTS "${render_output}")
    message(FATAL_ERROR
        "installed CLI catalog-discovery smoke failed\n"
        "exit: ${cli_result}\nstdout: ${cli_stdout}\nstderr: ${cli_stderr}")
endif()
