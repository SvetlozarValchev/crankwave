foreach(required IN ITEMS
        BUILD_DIRECTORY
        INSTALL_PREFIX
        INSTALL_BINDIR
        INSTALL_DATADIR
        INSTALL_LIBEXECDIR
        NODE_EXECUTABLE
        RELEASE_IDENTITY
        SOURCE_ROOT
        TAR_EXECUTABLE)
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
    "${INSTALL_PREFIX}/${INSTALL_DATADIR}/engine-sim-offline/${RELEASE_IDENTITY}")
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
        "${resource_root}/licenses/ENGINE-SIM-OFFLINE.txt"
        "${resource_root}/licenses/THIRD-PARTY-NOTICES.md"
        "${resource_root}/release.json"
        "${resource_root}/release.json.sha256"
        "${resource_root}/contracts/revengine-bake-workflow.v1.json"
        "${resource_root}/docs/contracts/CLI_RESULT_V1.md"
        "${resource_root}/docs/contracts/INSTALLED_DISTRIBUTION_V1.md"
        "${resource_root}/docs/contracts/RESPONSIVE_PROFILE_SELECTION_V1.md"
        "${resource_root}/docs/contracts/IR_AUTHORING_CATALOG_V1.md"
        "${resource_root}/assets/catalog.v1.json"
        "${resource_root}/assets/ir-authoring-catalog.v1.json"
        "${baker_root}/bake.mjs"
        "${baker_root}/dump-ir-spectrum.cpp"
        "${baker_root}/profiles/interactive-preview-v1.json"
        "${resource_root}/schemas/responsive-audio-bake-profile.schema.json"
        "${resource_root}/schemas/ir-authoring-catalog.schema.json")
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
foreach(required_internal IN ITEMS
        bake-contract.mjs
        directional-transients.mjs
        held-texture.mjs
        lifecycle.mjs)
    if(NOT required_internal IN_LIST internal_entries)
        message(FATAL_ERROR
            "installed baker internal stage is absent: ${required_internal}")
    endif()
endforeach()
foreach(internal_entry IN LISTS internal_entries)
    if(NOT internal_entry MATCHES "^[A-Za-z0-9._-]+\\.mjs$")
        message(FATAL_ERROR
            "installed baker internal closure has a non-module entry: ${internal_entry}")
    endif()
endforeach()

file(GLOB profile_entries
    RELATIVE "${baker_root}/profiles" "${baker_root}/profiles/*")
list(SORT profile_entries)
if(NOT "interactive-preview-v1.json" IN_LIST profile_entries)
    message(FATAL_ERROR "installed responsive profile closure is incomplete")
endif()
foreach(profile_entry IN LISTS profile_entries)
    if(NOT profile_entry MATCHES "^[A-Za-z0-9._-]+\\.json$")
        message(FATAL_ERROR
            "installed responsive profile closure has a non-JSON entry: ${profile_entry}")
    endif()
endforeach()

file(GLOB runtime_entries RELATIVE "${runtime_root}" "${runtime_root}/*")
list(SORT runtime_entries)
set(expected_runtime_entries
    c-api-abi.js
    c-api-client.js
    c-api-errors.js
    c-api-session.js
    directional-phase-cell.js
    dry-directional-phase-runtime.js
    held-phase-texture-runtime.js
    held-texture-presentation-runtime.js
    release.js
    renderer-runtime-compatibility.js
    responsive-audio-lifecycle-runtime.js
    revengine-audio-engine.js
    revengine-package.js
    shared-recorded-starter-runtime.js
    state-phase-texture-runtime.js
    steady-transient-envelope.js
    wasm-heap.js)
if(NOT "${runtime_entries}" STREQUAL "${expected_runtime_entries}")
    message(FATAL_ERROR
        "installed C API JavaScript closure differs: ${runtime_entries}")
endif()

file(READ "${resource_root}/package.json" package_metadata)
string(JSON package_type GET "${package_metadata}" type)
string(JSON node_requirement GET "${package_metadata}" engines node)
string(JSON package_version GET "${package_metadata}" version)
if(NOT package_type STREQUAL "module" OR
   NOT node_requirement STREQUAL ">=20.11.0" OR
   NOT package_version STREQUAL RELEASE_IDENTITY)
    message(FATAL_ERROR "installed Node package metadata is invalid")
endif()

execute_process(
    COMMAND "${cli}" --version
    RESULT_VARIABLE cli_version_result
    OUTPUT_VARIABLE cli_version_stdout
    ERROR_VARIABLE cli_version_stderr
)
if(NOT cli_version_result EQUAL 0 OR
   NOT cli_version_stdout STREQUAL
       "engine-sim-offline ${RELEASE_IDENTITY}\n" OR
   NOT cli_version_stderr STREQUAL "")
    message(FATAL_ERROR
        "installed CLI release identity differs\n"
        "exit: ${cli_version_result}\n"
        "stdout: ${cli_version_stdout}\nstderr: ${cli_version_stderr}")
endif()

execute_process(
    COMMAND "${cli}" inspect-ir-catalog --result-format json
    RESULT_VARIABLE ir_catalog_result
    OUTPUT_VARIABLE ir_catalog_stdout
    ERROR_VARIABLE ir_catalog_stderr
)
if(NOT ir_catalog_result EQUAL 0 OR NOT ir_catalog_stderr STREQUAL "")
    message(FATAL_ERROR
        "installed IR authoring catalog query failed\n"
        "exit: ${ir_catalog_result}\nstdout: ${ir_catalog_stdout}\n"
        "stderr: ${ir_catalog_stderr}")
endif()
string(JSON ir_result_schema GET "${ir_catalog_stdout}" schema)
string(JSON ir_result_release GET "${ir_catalog_stdout}" release_identity)
string(JSON ir_result_command GET "${ir_catalog_stdout}" command)
string(JSON ir_result_ok GET "${ir_catalog_stdout}" ok)
string(JSON ir_result_count GET "${ir_catalog_stdout}" result entry_count)
string(JSON ir_result_sha GET "${ir_catalog_stdout}" result catalog_sha256)
string(JSON ir_catalog_schema GET "${ir_catalog_stdout}" result catalog schema)
string(JSON ir_catalog_release GET
    "${ir_catalog_stdout}" result catalog release_identity)
string(JSON ir_catalog_entry_count LENGTH
    "${ir_catalog_stdout}" result catalog entries)
file(SHA256 "${resource_root}/assets/ir-authoring-catalog.v1.json"
    installed_ir_catalog_sha)
if(NOT ir_result_schema STREQUAL "engine-sim-offline.cli-result.v1" OR
   NOT ir_result_release STREQUAL RELEASE_IDENTITY OR
   NOT ir_result_command STREQUAL "inspect-ir-catalog" OR
   NOT ir_result_ok OR
   NOT ir_result_count EQUAL 73 OR
   NOT ir_result_sha STREQUAL installed_ir_catalog_sha OR
   NOT ir_catalog_schema STREQUAL
       "engine-sim-offline/ir-authoring-catalog.v1" OR
   NOT ir_catalog_release STREQUAL RELEASE_IDENTITY OR
   NOT ir_catalog_entry_count EQUAL 73)
    message(FATAL_ERROR
        "installed IR authoring query or release binding differs")
endif()

math(EXPR ir_catalog_last "${ir_catalog_entry_count} - 1")
foreach(ir_catalog_index RANGE 0 ${ir_catalog_last})
    string(JSON ir_payload_sha GET "${ir_catalog_stdout}"
        result catalog entries ${ir_catalog_index} sha256)
    set(ir_payload
        "${resource_root}/assets/payloads/${ir_payload_sha}")
    if(NOT EXISTS "${ir_payload}" OR IS_DIRECTORY "${ir_payload}")
        message(FATAL_ERROR
            "installed IR catalog payload is absent: ${ir_payload_sha}")
    endif()
    file(SHA256 "${ir_payload}" ir_payload_actual_sha)
    if(NOT ir_payload_actual_sha STREQUAL ir_payload_sha)
        message(FATAL_ERROR
            "installed IR catalog payload hash differs: ${ir_payload_sha}")
    endif()
endforeach()

execute_process(
    COMMAND "${launcher}" --version
    RESULT_VARIABLE launcher_version_result
    OUTPUT_VARIABLE launcher_version_stdout
    ERROR_VARIABLE launcher_version_stderr
)
if(NOT launcher_version_result EQUAL 0 OR
   NOT launcher_version_stdout STREQUAL
       "engine-sim-offline-responsive-bake ${RELEASE_IDENTITY}\n" OR
   NOT launcher_version_stderr STREQUAL "")
    message(FATAL_ERROR
        "installed responsive-bake release identity differs\n"
        "exit: ${launcher_version_result}\n"
        "stdout: ${launcher_version_stdout}\n"
        "stderr: ${launcher_version_stderr}")
endif()

execute_process(
    COMMAND "${launcher}" --help
    RESULT_VARIABLE launcher_help_result
    OUTPUT_VARIABLE launcher_help_stdout
    ERROR_VARIABLE launcher_help_stderr
)
if(NOT launcher_help_result EQUAL 0 OR
   NOT launcher_help_stdout MATCHES "^usage:\n" OR
   NOT launcher_help_stderr STREQUAL "")
    message(FATAL_ERROR
        "installed responsive-bake help failed\n"
        "exit: ${launcher_help_result}\n"
        "stdout: ${launcher_help_stdout}\n"
        "stderr: ${launcher_help_stderr}")
endif()

find_program(dirname_executable NAMES dirname REQUIRED)
set(node_missing_path "${INSTALL_PREFIX}.node-missing-path")
file(REMOVE_RECURSE "${node_missing_path}")
file(MAKE_DIRECTORY "${node_missing_path}")
file(CREATE_LINK "${dirname_executable}" "${node_missing_path}/dirname"
    SYMBOLIC)
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "PATH=${node_missing_path}"
        "ENGINE_SIM_OFFLINE_NODE=${NODE_EXECUTABLE}"
        "${launcher}" --plan
    RESULT_VARIABLE node_preflight_result
    OUTPUT_VARIABLE node_preflight_stdout
    ERROR_VARIABLE node_preflight_stderr
)
if(NOT node_preflight_result EQUAL 69 OR
   NOT node_preflight_stdout STREQUAL "")
    message(FATAL_ERROR
        "installed launcher Node preflight failed\n"
        "exit: ${node_preflight_result}\n"
        "stdout: ${node_preflight_stdout}\n"
        "stderr: ${node_preflight_stderr}")
endif()
string(JSON node_preflight_schema GET "${node_preflight_stderr}" schema)
string(JSON node_preflight_release GET
    "${node_preflight_stderr}" release_identity)
string(JSON node_preflight_code GET "${node_preflight_stderr}" code)
if(NOT node_preflight_schema STREQUAL
       "engine-sim-offline/responsive-audio-bake-failure-v1" OR
   NOT node_preflight_release STREQUAL RELEASE_IDENTITY OR
   NOT node_preflight_code STREQUAL "unavailable")
    message(FATAL_ERROR
        "installed launcher Node preflight JSON contract differs")
endif()
file(REMOVE_RECURSE "${node_missing_path}")

set(missing_resource_prefix "${INSTALL_PREFIX}.missing-resource-probe")
set(missing_resource_bin
    "${missing_resource_prefix}/${INSTALL_BINDIR}")
file(REMOVE_RECURSE "${missing_resource_prefix}")
file(MAKE_DIRECTORY "${missing_resource_bin}")
file(COPY "${launcher}" DESTINATION "${missing_resource_bin}")
set(missing_resource_launcher
    "${missing_resource_bin}/engine-sim-offline-responsive-bake")
execute_process(
    COMMAND "${missing_resource_launcher}" --plan
    RESULT_VARIABLE missing_resource_result
    OUTPUT_VARIABLE missing_resource_stdout
    ERROR_VARIABLE missing_resource_stderr
)
if(NOT missing_resource_result EQUAL 69 OR
   NOT missing_resource_stdout STREQUAL "")
    message(FATAL_ERROR
        "installed launcher missing-resource preflight failed\n"
        "exit: ${missing_resource_result}\n"
        "stdout: ${missing_resource_stdout}\n"
        "stderr: ${missing_resource_stderr}")
endif()
string(JSON missing_resource_schema GET
    "${missing_resource_stderr}" schema)
string(JSON missing_resource_release GET
    "${missing_resource_stderr}" release_identity)
string(JSON missing_resource_code GET
    "${missing_resource_stderr}" code)
if(NOT missing_resource_schema STREQUAL
       "engine-sim-offline/responsive-audio-bake-failure-v1" OR
   NOT missing_resource_release STREQUAL RELEASE_IDENTITY OR
   NOT missing_resource_code STREQUAL "unavailable")
    message(FATAL_ERROR
        "installed launcher missing-resource JSON contract differs")
endif()
file(REMOVE_RECURSE "${missing_resource_prefix}")

execute_process(
    COMMAND "${helper}" --version
    RESULT_VARIABLE helper_version_result
    OUTPUT_VARIABLE helper_version_stdout
    ERROR_VARIABLE helper_version_stderr
)
if(NOT helper_version_result EQUAL 0 OR
   NOT helper_version_stdout STREQUAL
       "dump-ir-spectrum ${RELEASE_IDENTITY}\n" OR
   NOT helper_version_stderr STREQUAL "")
    message(FATAL_ERROR
        "installed helper release identity differs\n"
        "exit: ${helper_version_result}\n"
        "stdout: ${helper_version_stdout}\nstderr: ${helper_version_stderr}")
endif()

execute_process(
    COMMAND
        "${NODE_EXECUTABLE}" --input-type=module --eval
        "const m=await import(process.argv[1]); if(m.ENGINE_SIM_OFFLINE_RELEASE_IDENTITY!==process.argv[2]) process.exit(2);"
        "${resource_root}/web/runtime/release.js"
        "${RELEASE_IDENTITY}"
    RESULT_VARIABLE javascript_release_result
    OUTPUT_VARIABLE javascript_release_stdout
    ERROR_VARIABLE javascript_release_stderr
)
if(NOT javascript_release_result EQUAL 0 OR
   NOT javascript_release_stdout STREQUAL "" OR
   NOT javascript_release_stderr STREQUAL "")
    message(FATAL_ERROR
        "installed JavaScript release identity differs\n"
        "exit: ${javascript_release_result}\n"
        "stdout: ${javascript_release_stdout}\n"
        "stderr: ${javascript_release_stderr}")
endif()

set(workflow_path
    "${resource_root}/contracts/revengine-bake-workflow.v1.json")
file(READ "${workflow_path}" workflow)
string(JSON workflow_schema GET "${workflow}" schema)
string(JSON workflow_release GET "${workflow}" release_identity)
string(JSON workflow_step_count LENGTH "${workflow}" steps)
string(JSON workflow_profile_policy GET
    "${workflow}" responsive_profile_selection default_policy)
string(JSON workflow_profile_id GET
    "${workflow}" responsive_profile_selection profile_id)
if(NOT workflow_schema STREQUAL
       "engine-sim-offline/revengine-bake-workflow.v1" OR
   NOT workflow_release STREQUAL RELEASE_IDENTITY OR
   NOT workflow_profile_policy STREQUAL "engine-redline-affine-v1" OR
   NOT workflow_profile_id STREQUAL "interactive-preview-redline-v1" OR
   NOT workflow_step_count EQUAL 3)
    message(FATAL_ERROR "installed REVENGINE bake workflow identity is invalid")
endif()
set(expected_workflow_ids responsive_bake pack verify)
set(expected_workflow_executables
    "${INSTALL_BINDIR}/engine-sim-offline-responsive-bake"
    "${INSTALL_BINDIR}/engine-sim-offline"
    "${INSTALL_BINDIR}/engine-sim-offline")
foreach(workflow_index RANGE 0 2)
    string(JSON workflow_ordinal GET "${workflow}" steps ${workflow_index} ordinal)
    string(JSON workflow_id GET "${workflow}" steps ${workflow_index} id)
    string(JSON workflow_executable
        GET "${workflow}" steps ${workflow_index} executable)
    math(EXPR expected_workflow_ordinal "${workflow_index} + 1")
    list(GET expected_workflow_ids ${workflow_index} expected_workflow_id)
    list(GET expected_workflow_executables ${workflow_index}
         expected_workflow_executable)
    if(NOT workflow_ordinal EQUAL expected_workflow_ordinal OR
       NOT workflow_id STREQUAL expected_workflow_id OR
       NOT workflow_executable STREQUAL expected_workflow_executable)
        message(FATAL_ERROR
            "installed REVENGINE bake workflow sequence differs at ${workflow_index}")
    endif()
endforeach()
string(JSON responsive_argument_count LENGTH "${workflow}" steps 0 arguments)
math(EXPR responsive_argument_last "${responsive_argument_count} - 1")
set(responsive_arguments)
foreach(responsive_argument_index RANGE 0 ${responsive_argument_last})
    string(JSON responsive_argument GET
        "${workflow}" steps 0 arguments ${responsive_argument_index})
    list(APPEND responsive_arguments "${responsive_argument}")
endforeach()
set(responsive_deadline_flags ${responsive_arguments})
list(FILTER responsive_deadline_flags INCLUDE
    REGEX "^--deadline-unix-ms$")
list(LENGTH responsive_deadline_flags responsive_deadline_count)
list(FIND responsive_arguments "--deadline-unix-ms"
    responsive_deadline_index)
math(EXPR responsive_deadline_value_index
    "${responsive_deadline_index} + 1")
if(responsive_deadline_index GREATER_EQUAL 0 AND
   responsive_deadline_value_index LESS responsive_argument_count)
    list(GET responsive_arguments ${responsive_deadline_value_index}
        responsive_deadline_value)
else()
    set(responsive_deadline_value "")
endif()
if("--profile" IN_LIST responsive_arguments OR
   NOT responsive_deadline_count EQUAL 1 OR
   NOT responsive_deadline_value STREQUAL "{deadline_unix_ms}")
    message(FATAL_ERROR
        "default installed workflow bypasses profile policy or deadline")
endif()
string(JSON responsive_stdout_schema GET
    "${workflow}" steps 0 required_stdout_record schema)
string(JSON responsive_stdout_release GET
    "${workflow}" steps 0 required_stdout_record release_identity)
string(JSON responsive_stdout_completed GET
    "${workflow}" steps 0 required_stdout_record completed)
if(NOT responsive_stdout_schema STREQUAL
       "engine-sim-offline/responsive-audio-bake-plan-v1" OR
   NOT responsive_stdout_release STREQUAL RELEASE_IDENTITY OR
   NOT responsive_stdout_completed)
    message(FATAL_ERROR
        "installed responsive-bake workflow success contract differs")
endif()
foreach(machine_step_index RANGE 1 2)
    string(JSON machine_argument_count LENGTH
        "${workflow}" steps ${machine_step_index} arguments)
    math(EXPR machine_argument_last "${machine_argument_count} - 1")
    set(machine_arguments)
    foreach(machine_argument_index RANGE 0 ${machine_argument_last})
        string(JSON machine_argument GET
            "${workflow}" steps ${machine_step_index} arguments
            ${machine_argument_index})
        list(APPEND machine_arguments "${machine_argument}")
    endforeach()
    set(machine_deadline_flags ${machine_arguments})
    list(FILTER machine_deadline_flags INCLUDE
        REGEX "^--deadline-unix-ms$")
    list(LENGTH machine_deadline_flags machine_deadline_count)
    list(FIND machine_arguments "--deadline-unix-ms"
        machine_deadline_index)
    math(EXPR machine_deadline_value_index
        "${machine_deadline_index} + 1")
    if(machine_deadline_index GREATER_EQUAL 0 AND
       machine_deadline_value_index LESS machine_argument_count)
        list(GET machine_arguments ${machine_deadline_value_index}
            machine_deadline_value)
    else()
        set(machine_deadline_value "")
    endif()
    if(NOT "--result-format" IN_LIST machine_arguments OR
       NOT "json" IN_LIST machine_arguments OR
       NOT machine_deadline_count EQUAL 1 OR
       NOT machine_deadline_value STREQUAL "{deadline_unix_ms}")
        message(FATAL_ERROR
            "installed REVENGINE workflow does not request machine results and deadline")
    endif()
    string(JSON machine_stdout_schema GET "${workflow}" steps
        ${machine_step_index} required_stdout_record schema)
    string(JSON machine_stdout_release GET "${workflow}" steps
        ${machine_step_index} required_stdout_record release_identity)
    string(JSON machine_stdout_ok GET "${workflow}" steps
        ${machine_step_index} required_stdout_record ok)
    string(JSON machine_stdout_code GET "${workflow}" steps
        ${machine_step_index} required_stdout_record code)
    string(JSON machine_stdout_exit GET "${workflow}" steps
        ${machine_step_index} required_stdout_record exit_code)
    if(NOT machine_stdout_schema STREQUAL
           "engine-sim-offline.cli-result.v1" OR
       NOT machine_stdout_release STREQUAL RELEASE_IDENTITY OR
       NOT machine_stdout_ok OR
       NOT machine_stdout_code STREQUAL "success" OR
       NOT machine_stdout_exit EQUAL 0)
        message(FATAL_ERROR
            "installed REVENGINE workflow machine success contract differs")
    endif()
endforeach()
string(JSON workflow_pack_command GET
    "${workflow}" steps 1 required_stdout_record command)
string(JSON workflow_pack_output GET
    "${workflow}" steps 1 required_result_fields output_file)
string(JSON workflow_verify_command GET
    "${workflow}" steps 2 required_stdout_record command)
string(JSON workflow_verify_verified GET
    "${workflow}" steps 2 required_result_fields verified)
if(NOT workflow_pack_command STREQUAL "pack-revengine" OR
   NOT workflow_pack_output STREQUAL "{new_revengine_file}" OR
   NOT workflow_verify_command STREQUAL "verify-revengine" OR
   NOT workflow_verify_verified)
    message(FATAL_ERROR
        "installed REVENGINE workflow typed machine results differ")
endif()
string(JSON workflow_telemetry_role GET
    "${workflow}" telemetry_contract role)
string(JSON workflow_telemetry_kind GET
    "${workflow}" telemetry_contract kind)
string(JSON workflow_telemetry_path GET
    "${workflow}" telemetry_contract path)
string(JSON workflow_telemetry_schema GET
    "${workflow}" telemetry_contract schema)
string(JSON workflow_telemetry_commit GET
    "${workflow}" telemetry_contract originating_commit)
if(NOT workflow_telemetry_role STREQUAL
       "diagnostics.engine-telemetry.v1" OR
   NOT workflow_telemetry_kind STREQUAL "telemetry" OR
   NOT workflow_telemetry_path STREQUAL
       "telemetry/engine-telemetry.v1.ndjson" OR
   NOT workflow_telemetry_schema STREQUAL
       "engine-sim-offline.engine-telemetry.ndjson.v1" OR
   NOT workflow_telemetry_commit STREQUAL
       "c8d672b59e3046654ad5f818f31725798aef7ffa")
    message(FATAL_ERROR "installed workflow telemetry binding differs")
endif()

set(release_manifest_path "${resource_root}/release.json")
file(READ "${release_manifest_path}" release_manifest)
file(READ "${resource_root}/release.json.sha256" release_binding)
file(SHA256 "${release_manifest_path}" actual_release_binding)
if(NOT release_binding STREQUAL "${actual_release_binding}\n")
    message(FATAL_ERROR "installed distribution binding digest differs")
endif()
string(JSON release_schema GET "${release_manifest}" schema)
string(JSON release_identity GET "${release_manifest}" release_identity)
string(JSON release_resource_root GET
    "${release_manifest}" layout resource_root)
string(JSON release_native_path GET
    "${release_manifest}" renderers native path)
string(JSON release_native_sha256 GET
    "${release_manifest}" renderers native sha256)
string(JSON release_classification GET
    "${release_manifest}" classification)
string(JSON release_complete GET "${release_manifest}" complete)
string(JSON release_source_state GET "${release_manifest}" source state)
string(JSON release_renderer_source_state GET
    "${release_manifest}" source renderer_state)
string(JSON release_installed_inputs_state GET
    "${release_manifest}" source installed_inputs_state)
string(JSON release_git_commit GET "${release_manifest}" source git_commit)
string(JSON release_source_closure GET
    "${release_manifest}" source closure_sha256)
string(JSON release_installed_inputs_closure GET
    "${release_manifest}" source installed_inputs_closure_sha256)
string(JSON release_toolchain_state GET
    "${release_manifest}" toolchain state)
string(JSON release_wasm_type TYPE "${release_manifest}" renderers wasm)
string(JSON release_telemetry_role GET
    "${release_manifest}" telemetry_contract role)
string(JSON release_telemetry_kind GET
    "${release_manifest}" telemetry_contract kind)
string(JSON release_telemetry_path GET
    "${release_manifest}" telemetry_contract path)
string(JSON release_telemetry_schema GET
    "${release_manifest}" telemetry_contract schema)
string(JSON release_telemetry_commit GET
    "${release_manifest}" telemetry_contract originating_commit)
file(SHA256 "${cli}" actual_native_sha256)
file(RELATIVE_PATH expected_resource_root "${INSTALL_PREFIX}" "${resource_root}")
file(TO_CMAKE_PATH "${expected_resource_root}" expected_resource_root)
if(NOT release_schema STREQUAL
       "engine-sim-offline/installed-distribution.v1" OR
   NOT release_identity STREQUAL RELEASE_IDENTITY OR
   NOT release_resource_root STREQUAL expected_resource_root OR
   NOT release_native_path STREQUAL "${INSTALL_BINDIR}/engine-sim-offline" OR
   NOT release_native_sha256 STREQUAL actual_native_sha256 OR
   NOT release_telemetry_role STREQUAL
       "diagnostics.engine-telemetry.v1" OR
   NOT release_telemetry_kind STREQUAL "telemetry" OR
   NOT release_telemetry_path STREQUAL
       "telemetry/engine-telemetry.v1.ndjson" OR
   NOT release_telemetry_schema STREQUAL
       "engine-sim-offline.engine-telemetry.ndjson.v1" OR
   NOT release_telemetry_commit STREQUAL
       "c8d672b59e3046654ad5f818f31725798aef7ffa")
    message(FATAL_ERROR "installed distribution identity metadata differs")
endif()
if(NOT release_git_commit MATCHES "^[0-9a-f]+$" OR
   NOT release_source_closure MATCHES "^[0-9a-f]+$" OR
   NOT release_installed_inputs_closure MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "installed distribution source identity is malformed")
endif()
string(LENGTH "${release_git_commit}" release_git_commit_length)
string(LENGTH "${release_source_closure}" release_source_closure_length)
string(LENGTH "${release_installed_inputs_closure}"
    release_installed_inputs_closure_length)
if(NOT (release_git_commit_length EQUAL 40 OR
        release_git_commit_length EQUAL 64) OR
   NOT release_source_closure_length EQUAL 64 OR
   NOT release_installed_inputs_closure_length EQUAL 64)
    message(FATAL_ERROR "installed distribution source identity has wrong width")
endif()
if(release_wasm_type STREQUAL "OBJECT")
    string(JSON release_wasm_loader_path GET
        "${release_manifest}" renderers wasm loader_path)
    string(JSON release_wasm_loader_sha256 GET
        "${release_manifest}" renderers wasm loader_sha256)
    string(JSON release_wasm_module_path GET
        "${release_manifest}" renderers wasm module_path)
    string(JSON release_wasm_module_sha256 GET
        "${release_manifest}" renderers wasm module_sha256)
    string(JSON release_wasm_source_closure GET
        "${release_manifest}" renderers wasm source_closure_sha256)
    string(JSON release_wasm_source_matches GET
        "${release_manifest}" renderers wasm source_closure_matches_native)
    file(SHA256 "${INSTALL_PREFIX}/${release_wasm_loader_path}"
         actual_wasm_loader_sha256)
    file(SHA256 "${INSTALL_PREFIX}/${release_wasm_module_path}"
         actual_wasm_module_sha256)
    if(NOT release_wasm_loader_sha256 STREQUAL actual_wasm_loader_sha256 OR
       NOT release_wasm_module_sha256 STREQUAL actual_wasm_module_sha256)
        message(FATAL_ERROR "installed WebAssembly renderer identity differs")
    endif()
endif()
if(release_complete)
    if(NOT release_classification STREQUAL "immutable_release" OR
       NOT release_source_state STREQUAL "clean" OR
       NOT release_renderer_source_state STREQUAL "clean" OR
       NOT release_installed_inputs_state STREQUAL "clean" OR
       NOT release_toolchain_state STREQUAL "available" OR
       NOT release_wasm_type STREQUAL "OBJECT" OR
       NOT release_wasm_source_matches OR
       NOT release_wasm_source_closure STREQUAL release_source_closure)
        message(FATAL_ERROR
            "complete installed distribution identity is inconsistent")
    endif()
elseif(NOT release_classification STREQUAL
       "incomplete_development_install")
    message(FATAL_ERROR
        "incomplete installed distribution masquerades as a release")
endif()
file(SHA256 "${workflow_path}" actual_workflow_sha256)
string(JSON release_workflow_sha256 GET
    "${release_manifest}" revengine_bake_workflow sha256)
if(NOT release_workflow_sha256 STREQUAL actual_workflow_sha256)
    message(FATAL_ERROR "installed bake workflow binding differs")
endif()
string(JSON release_file_count LENGTH "${release_manifest}" files)
if(release_file_count LESS 1)
    message(FATAL_ERROR "installed distribution file closure is empty")
endif()
math(EXPR release_file_last "${release_file_count} - 1")
set(prior_release_file "")
set(manifest_release_files)
foreach(release_file_index RANGE 0 ${release_file_last})
    string(JSON release_file GET
        "${release_manifest}" files ${release_file_index} path)
    string(JSON release_file_bytes GET
        "${release_manifest}" files ${release_file_index} bytes)
    string(JSON release_file_sha256 GET
        "${release_manifest}" files ${release_file_index} sha256)
    if(NOT prior_release_file STREQUAL "" AND
       NOT prior_release_file STRLESS release_file)
        message(FATAL_ERROR "installed distribution file closure is not ordered")
    endif()
    set(prior_release_file "${release_file}")
    list(APPEND manifest_release_files "${release_file}")
    set(release_file_path "${INSTALL_PREFIX}/${release_file}")
    if(NOT EXISTS "${release_file_path}" OR IS_DIRECTORY "${release_file_path}")
        message(FATAL_ERROR
            "installed distribution member is absent: ${release_file}")
    endif()
    file(SIZE "${release_file_path}" actual_release_file_bytes)
    file(SHA256 "${release_file_path}" actual_release_file_sha256)
    if(NOT release_file_bytes EQUAL actual_release_file_bytes OR
       NOT release_file_sha256 STREQUAL actual_release_file_sha256)
        message(FATAL_ERROR
            "installed distribution member differs: ${release_file}")
    endif()
endforeach()

file(GLOB_RECURSE actual_resource_files LIST_DIRECTORIES FALSE
    "${resource_root}/*")
list(REMOVE_ITEM actual_resource_files
    "${resource_root}/release.json"
    "${resource_root}/release.json.sha256")
list(APPEND actual_resource_files "${cli}" "${launcher}" "${helper}")
set(actual_release_files)
foreach(actual_resource_file IN LISTS actual_resource_files)
    file(RELATIVE_PATH actual_release_file
        "${INSTALL_PREFIX}" "${actual_resource_file}")
    file(TO_CMAKE_PATH "${actual_release_file}" actual_release_file)
    list(APPEND actual_release_files "${actual_release_file}")
endforeach()
list(REMOVE_DUPLICATES actual_release_files)
list(SORT actual_release_files)
if(NOT "${actual_release_files}" STREQUAL "${manifest_release_files}")
    message(FATAL_ERROR
        "installed distribution manifest omits or invents a member")
endif()

get_filename_component(install_prefix_name "${INSTALL_PREFIX}" NAME)
set(repeat_install_parent "${INSTALL_PREFIX}.repeat-parent")
set(repeat_install_prefix
    "${repeat_install_parent}/${install_prefix_name}")
file(REMOVE_RECURSE "${repeat_install_parent}")
set(repeat_install_command
    "${CMAKE_COMMAND}" --install "${BUILD_DIRECTORY}"
    --prefix "${repeat_install_prefix}")
if(DEFINED INSTALL_CONFIG AND NOT INSTALL_CONFIG STREQUAL "")
    list(APPEND repeat_install_command --config "${INSTALL_CONFIG}")
endif()
execute_process(
    COMMAND ${repeat_install_command}
    RESULT_VARIABLE repeat_install_result
    OUTPUT_VARIABLE repeat_install_stdout
    ERROR_VARIABLE repeat_install_stderr
)
set(repeat_manifest
    "${repeat_install_prefix}/${INSTALL_DATADIR}/engine-sim-offline/${RELEASE_IDENTITY}/release.json")
if(NOT repeat_install_result EQUAL 0 OR NOT EXISTS "${repeat_manifest}")
    message(FATAL_ERROR
        "repeated prefix install failed\n"
        "exit: ${repeat_install_result}\n"
        "stdout: ${repeat_install_stdout}\nstderr: ${repeat_install_stderr}")
endif()
file(SHA256 "${repeat_manifest}" repeat_release_binding)
if(NOT repeat_release_binding STREQUAL actual_release_binding)
    message(FATAL_ERROR
        "repeated installed distribution produced a different binding")
endif()

# Prove that caller debris beside the installed closure cannot enter a release
# archive or perturb its bytes.
set(unmanifested_archive_probe
    "${INSTALL_PREFIX}/unmanifested-archive-probe.txt")
file(WRITE "${unmanifested_archive_probe}" "not a distribution member\n")
set(first_archive "${INSTALL_PREFIX}.first.tar")
set(second_archive "${INSTALL_PREFIX}.second.tar")
foreach(archive_path IN ITEMS "${first_archive}" "${second_archive}")
    file(REMOVE "${archive_path}" "${archive_path}.sha256")
endforeach()
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        "-DPREFIX_DIRECTORY=${INSTALL_PREFIX}"
        "-DRESOURCE_RELATIVE_DIRECTORY=${INSTALL_DATADIR}/engine-sim-offline/${RELEASE_IDENTITY}"
        "-DRELEASE_IDENTITY=${RELEASE_IDENTITY}"
        "-DOUTPUT_ARCHIVE=${first_archive}"
        "-DTAR_EXECUTABLE=${TAR_EXECUTABLE}"
        -DREQUIRE_COMPLETE=FALSE
        -P
        "${SOURCE_ROOT}/cmake/CreateDeterministicDistributionArchive.cmake"
    RESULT_VARIABLE first_archive_result
    OUTPUT_VARIABLE first_archive_stdout
    ERROR_VARIABLE first_archive_stderr
)
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        "-DPREFIX_DIRECTORY=${repeat_install_prefix}"
        "-DRESOURCE_RELATIVE_DIRECTORY=${INSTALL_DATADIR}/engine-sim-offline/${RELEASE_IDENTITY}"
        "-DRELEASE_IDENTITY=${RELEASE_IDENTITY}"
        "-DOUTPUT_ARCHIVE=${second_archive}"
        "-DTAR_EXECUTABLE=${TAR_EXECUTABLE}"
        -DREQUIRE_COMPLETE=FALSE
        -P
        "${SOURCE_ROOT}/cmake/CreateDeterministicDistributionArchive.cmake"
    RESULT_VARIABLE second_archive_result
    OUTPUT_VARIABLE second_archive_stdout
    ERROR_VARIABLE second_archive_stderr
)
if(NOT first_archive_result EQUAL 0 OR
   NOT second_archive_result EQUAL 0 OR
   NOT first_archive_stdout STREQUAL "" OR
   NOT second_archive_stdout STREQUAL "" OR
   NOT first_archive_stderr STREQUAL "" OR
   NOT second_archive_stderr STREQUAL "")
    message(FATAL_ERROR
        "deterministic installed archive assembly failed\n"
        "first exit: ${first_archive_result}\n"
        "first stdout: ${first_archive_stdout}\n"
        "first stderr: ${first_archive_stderr}\n"
        "second exit: ${second_archive_result}\n"
        "second stdout: ${second_archive_stdout}\n"
        "second stderr: ${second_archive_stderr}")
endif()
file(SHA256 "${first_archive}" first_archive_sha256)
file(SHA256 "${second_archive}" second_archive_sha256)
if(NOT first_archive_sha256 STREQUAL second_archive_sha256)
    message(FATAL_ERROR
        "repeated installed distribution archive bytes differ")
endif()
foreach(archive_path IN ITEMS "${first_archive}" "${second_archive}")
    file(READ "${archive_path}.sha256" archive_sidecar)
    string(STRIP "${archive_sidecar}" archive_sidecar)
    if(NOT archive_sidecar STREQUAL first_archive_sha256)
        message(FATAL_ERROR "installed distribution archive sidecar differs")
    endif()
endforeach()
execute_process(
    COMMAND "${TAR_EXECUTABLE}" -tf "${first_archive}"
    RESULT_VARIABLE archive_list_result
    OUTPUT_VARIABLE archive_list_stdout
    ERROR_VARIABLE archive_list_stderr
)
if(NOT archive_list_result EQUAL 0 OR
   NOT archive_list_stderr STREQUAL "")
    message(FATAL_ERROR "installed distribution archive contents are invalid")
endif()
string(REPLACE "\n" ";" archive_members "${archive_list_stdout}")
list(FILTER archive_members EXCLUDE REGEX "^$")
set(normalized_archive_members)
foreach(archive_member IN LISTS archive_members)
    string(REGEX REPLACE "/$" "" archive_member "${archive_member}")
    list(APPEND normalized_archive_members "${archive_member}")
endforeach()
list(REMOVE_DUPLICATES normalized_archive_members)
list(SORT normalized_archive_members)

set(expected_archive_members "${install_prefix_name}")
foreach(release_file IN LISTS manifest_release_files)
    list(APPEND expected_archive_members
        "${install_prefix_name}/${release_file}")
endforeach()
list(APPEND expected_archive_members
    "${install_prefix_name}/${INSTALL_DATADIR}/engine-sim-offline/${RELEASE_IDENTITY}/release.json"
    "${install_prefix_name}/${INSTALL_DATADIR}/engine-sim-offline/${RELEASE_IDENTITY}/release.json.sha256")
set(expected_archive_file_members ${expected_archive_members})
foreach(expected_archive_file_member IN LISTS expected_archive_file_members)
    get_filename_component(expected_archive_parent
        "${expected_archive_file_member}" DIRECTORY)
    while(NOT expected_archive_parent STREQUAL "" AND
          NOT expected_archive_parent STREQUAL ".")
        list(APPEND expected_archive_members "${expected_archive_parent}")
        if(expected_archive_parent STREQUAL install_prefix_name)
            break()
        endif()
        get_filename_component(expected_archive_parent
            "${expected_archive_parent}" DIRECTORY)
    endwhile()
endforeach()
list(REMOVE_DUPLICATES expected_archive_members)
list(SORT expected_archive_members)
if(NOT "${normalized_archive_members}" STREQUAL
       "${expected_archive_members}")
    message(FATAL_ERROR
        "installed distribution archive closure differs")
endif()
file(REMOVE "${unmanifested_archive_probe}")
set(tampered_member
    "${repeat_install_prefix}/${INSTALL_DATADIR}/engine-sim-offline/${RELEASE_IDENTITY}/contracts/revengine-bake-workflow.v1.json")
file(APPEND "${tampered_member}" " ")
set(tampered_archive "${INSTALL_PREFIX}.tampered.tar")
file(REMOVE "${tampered_archive}" "${tampered_archive}.sha256")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        "-DPREFIX_DIRECTORY=${repeat_install_prefix}"
        "-DRESOURCE_RELATIVE_DIRECTORY=${INSTALL_DATADIR}/engine-sim-offline/${RELEASE_IDENTITY}"
        "-DRELEASE_IDENTITY=${RELEASE_IDENTITY}"
        "-DOUTPUT_ARCHIVE=${tampered_archive}"
        "-DTAR_EXECUTABLE=${TAR_EXECUTABLE}"
        -DREQUIRE_COMPLETE=FALSE
        -P
        "${SOURCE_ROOT}/cmake/CreateDeterministicDistributionArchive.cmake"
    RESULT_VARIABLE tampered_archive_result
    OUTPUT_VARIABLE tampered_archive_stdout
    ERROR_VARIABLE tampered_archive_stderr
)
if(tampered_archive_result EQUAL 0 OR
   NOT tampered_archive_stdout STREQUAL "" OR
   NOT tampered_archive_stderr MATCHES
       "distribution member identity differs" OR
   EXISTS "${tampered_archive}" OR EXISTS "${tampered_archive}.sha256")
    message(FATAL_ERROR
        "tampered installed distribution did not fail closed\n"
        "exit: ${tampered_archive_result}\n"
        "stdout: ${tampered_archive_stdout}\n"
        "stderr: ${tampered_archive_stderr}")
endif()
file(REMOVE_RECURSE "${repeat_install_parent}")
file(REMOVE
    "${first_archive}" "${first_archive}.sha256"
    "${second_archive}" "${second_archive}.sha256"
    "${tampered_archive}" "${tampered_archive}.sha256")

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
get_filename_component(node_directory "${NODE_EXECUTABLE}" DIRECTORY)
set(installed_launcher_environment
    "${CMAKE_COMMAND}" -E env
    "PATH=${node_directory}:$ENV{PATH}"
    "ENGINE_SIM_OFFLINE_NODE=${INSTALL_PREFIX}/ambient-node-must-not-run"
    "ENGINE_SIM_OFFLINE_BUILTIN_ASSETS=${INSTALL_PREFIX}/ambient-assets-must-not-run"
    "ENGINE_SIM_OFFLINE_WASM_MODULE=${INSTALL_PREFIX}/ambient-module-must-not-run"
    "CXX=${INSTALL_PREFIX}/ambient-compiler-must-not-run")
execute_process(
    COMMAND
        ${installed_launcher_environment}
        "${launcher}" --module "${INSTALL_PREFIX}/forbidden.js"
    RESULT_VARIABLE forbidden_override_result
    OUTPUT_VARIABLE forbidden_override_stdout
    ERROR_VARIABLE forbidden_override_stderr
)
if(NOT forbidden_override_result EQUAL 64 OR
   NOT forbidden_override_stdout STREQUAL "")
    message(FATAL_ERROR
        "installed launcher accepted a resource override\n"
        "exit: ${forbidden_override_result}\n"
        "stdout: ${forbidden_override_stdout}\n"
        "stderr: ${forbidden_override_stderr}")
endif()
string(JSON forbidden_override_schema GET
    "${forbidden_override_stderr}" schema)
string(JSON forbidden_override_release GET
    "${forbidden_override_stderr}" release_identity)
string(JSON forbidden_override_code GET
    "${forbidden_override_stderr}" code)
string(JSON forbidden_override_exit GET
    "${forbidden_override_stderr}" exit_code)
if(NOT forbidden_override_schema STREQUAL
       "engine-sim-offline/responsive-audio-bake-failure-v1" OR
   NOT forbidden_override_release STREQUAL RELEASE_IDENTITY OR
   NOT forbidden_override_code STREQUAL "invalid_invocation" OR
   NOT forbidden_override_exit EQUAL 64)
    message(FATAL_ERROR
        "installed launcher preflight failure contract differs")
endif()

execute_process(
    COMMAND
        ${installed_launcher_environment}
        "${launcher}"
        --engine "${INSTALL_PREFIX}/absent-engine.json"
        --output "${INSTALL_PREFIX}/failed-responsive-output"
        --cache "${INSTALL_PREFIX}/failed-responsive-cache"
    RESULT_VARIABLE baker_failure_result
    OUTPUT_VARIABLE baker_failure_stdout
    ERROR_VARIABLE baker_failure_stderr
)
if(NOT baker_failure_result EQUAL 66 OR
   NOT baker_failure_stdout STREQUAL "" OR
   EXISTS "${INSTALL_PREFIX}/failed-responsive-output" OR
   EXISTS "${INSTALL_PREFIX}/failed-responsive-cache")
    message(FATAL_ERROR
        "installed responsive baker failure contract failed\n"
        "exit: ${baker_failure_result}\n"
        "stdout: ${baker_failure_stdout}\n"
        "stderr: ${baker_failure_stderr}")
endif()
string(JSON baker_failure_schema GET "${baker_failure_stderr}" schema)
string(JSON baker_failure_release GET
    "${baker_failure_stderr}" release_identity)
string(JSON baker_failure_code GET "${baker_failure_stderr}" code)
string(JSON baker_failure_exit GET "${baker_failure_stderr}" exit_code)
if(NOT baker_failure_schema STREQUAL
       "engine-sim-offline/responsive-audio-bake-failure-v1" OR
   NOT baker_failure_release STREQUAL RELEASE_IDENTITY OR
   NOT baker_failure_code STREQUAL "input_unavailable" OR
   NOT baker_failure_exit EQUAL 66)
    message(FATAL_ERROR
        "installed responsive baker failure identity differs")
endif()
execute_process(
    COMMAND
        ${installed_launcher_environment}
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
string(JSON plan_release_identity GET "${plan_stdout}" release_identity)
if(NOT plan_release_identity STREQUAL RELEASE_IDENTITY)
    message(FATAL_ERROR
        "installed responsive-baker plan release identity differs")
endif()

set(automatic_plan_output "${INSTALL_PREFIX}/automatic-responsive-plan-output")
set(automatic_plan_cache "${INSTALL_PREFIX}/automatic-responsive-plan-cache")
execute_process(
    COMMAND
        ${installed_launcher_environment}
        "${launcher}"
        --engine
        "${SOURCE_ROOT}/data/engines/kohler-ch750-cleanroom/engine.json"
        --output "${automatic_plan_output}"
        --cache "${automatic_plan_cache}"
        --plan
    WORKING_DIRECTORY "${INSTALL_PREFIX}"
    RESULT_VARIABLE automatic_plan_result
    OUTPUT_VARIABLE automatic_plan_stdout
    ERROR_VARIABLE automatic_plan_stderr
)
if(NOT automatic_plan_result EQUAL 0 OR
   NOT automatic_plan_stderr STREQUAL "" OR
   EXISTS "${automatic_plan_output}" OR EXISTS "${automatic_plan_cache}")
    message(FATAL_ERROR
        "installed automatic responsive-profile plan failed\n"
        "exit: ${automatic_plan_result}\n"
        "stdout: ${automatic_plan_stdout}\n"
        "stderr: ${automatic_plan_stderr}")
endif()
string(JSON automatic_profile GET "${automatic_plan_stdout}" profile)
string(JSON automatic_release_identity GET
    "${automatic_plan_stdout}" release_identity)
string(JSON automatic_anchor_count LENGTH
    "${automatic_plan_stdout}" rpm_anchors)
math(EXPR automatic_last_anchor_index "${automatic_anchor_count} - 1")
string(JSON automatic_last_anchor GET
    "${automatic_plan_stdout}" rpm_anchors ${automatic_last_anchor_index})
string(JSON automatic_held_cells GET
    "${automatic_plan_stdout}" held_cell_count)
if(NOT automatic_release_identity STREQUAL RELEASE_IDENTITY OR
   NOT automatic_profile STREQUAL "interactive-preview-redline-v1" OR
   NOT automatic_anchor_count EQUAL 11 OR
   NOT automatic_last_anchor EQUAL 3600 OR
   NOT automatic_held_cells EQUAL 33)
    message(FATAL_ERROR
        "installed automatic responsive-profile plan differs")
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
            ${installed_launcher_environment}
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
       NOT EXISTS "${real_bake_output}/revengine.json" OR
       NOT EXISTS "${real_bake_output}/bake-report.json")
        message(FATAL_ERROR
            "installed responsive-baker real smoke failed\n"
            "exit: ${real_bake_result}\n"
            "stdout: ${real_bake_stdout}\nstderr: ${real_bake_stderr}")
    endif()
    string(JSON real_bake_release GET
        "${real_bake_stdout}" release_identity)
    file(READ "${real_bake_output}/bake-report.json" real_bake_report)
    string(JSON real_bake_report_release GET
        "${real_bake_report}" release_identity)
    if(NOT real_bake_release STREQUAL RELEASE_IDENTITY OR
       NOT real_bake_report_release STREQUAL RELEASE_IDENTITY)
        message(FATAL_ERROR
            "installed responsive bake release identity differs")
    endif()

    set(repeat_bake_output
        "${INSTALL_PREFIX}/responsive-smoke-output-repeat")
    execute_process(
        COMMAND
            ${installed_launcher_environment}
            "${launcher}"
            --engine
            "${SOURCE_ROOT}/data/engines/bmw-m52tub28-cleanroom/engine.json"
            --profile
            "${SOURCE_ROOT}/tools/responsive-audio-baker/testdata/smoke-profile.json"
            --output "${repeat_bake_output}"
            --cache "${real_bake_cache}"
            --jobs 2
        WORKING_DIRECTORY "${INSTALL_PREFIX}"
        RESULT_VARIABLE repeat_bake_result
        OUTPUT_VARIABLE repeat_bake_stdout
        ERROR_VARIABLE repeat_bake_stderr
        TIMEOUT 90
    )
    if(NOT repeat_bake_result EQUAL 0 OR
       NOT EXISTS "${repeat_bake_output}/runtime.json" OR
       NOT EXISTS "${repeat_bake_output}/revengine.json" OR
       NOT EXISTS "${repeat_bake_output}/bake-report.json")
        message(FATAL_ERROR
            "repeated installed responsive bake failed\n"
            "exit: ${repeat_bake_result}\n"
            "stdout: ${repeat_bake_stdout}\n"
            "stderr: ${repeat_bake_stderr}")
    endif()
    string(JSON repeat_bake_release GET
        "${repeat_bake_stdout}" release_identity)
    if(NOT repeat_bake_release STREQUAL RELEASE_IDENTITY)
        message(FATAL_ERROR
            "repeated responsive bake release identity differs")
    endif()
    file(GLOB_RECURSE real_bake_files
        LIST_DIRECTORIES FALSE
        RELATIVE "${real_bake_output}"
        "${real_bake_output}/*")
    file(GLOB_RECURSE repeat_bake_files
        LIST_DIRECTORIES FALSE
        RELATIVE "${repeat_bake_output}"
        "${repeat_bake_output}/*")
    list(SORT real_bake_files)
    list(SORT repeat_bake_files)
    if(NOT "${real_bake_files}" STREQUAL "${repeat_bake_files}")
        message(FATAL_ERROR
            "repeated responsive bake package closure differs")
    endif()
    foreach(real_bake_file IN LISTS real_bake_files)
        file(SIZE "${real_bake_output}/${real_bake_file}"
            real_bake_file_size)
        file(SIZE "${repeat_bake_output}/${real_bake_file}"
            repeat_bake_file_size)
        file(SHA256 "${real_bake_output}/${real_bake_file}"
            real_bake_file_sha256)
        file(SHA256 "${repeat_bake_output}/${real_bake_file}"
            repeat_bake_file_sha256)
        if(NOT real_bake_file_size EQUAL repeat_bake_file_size OR
           NOT real_bake_file_sha256 STREQUAL repeat_bake_file_sha256)
            message(FATAL_ERROR
                "repeated responsive bake member differs: ${real_bake_file}")
        endif()
    endforeach()

    set(real_carrier "${INSTALL_PREFIX}/responsive-smoke.revengine")
    set(repeat_carrier
        "${INSTALL_PREFIX}/responsive-smoke-repeat.revengine")
    execute_process(
        COMMAND
            "${cli}" pack-revengine
            --package-directory "${real_bake_output}"
            --output "${real_carrier}"
            --result-format json
        RESULT_VARIABLE real_pack_result
        OUTPUT_VARIABLE real_pack_stdout
        ERROR_VARIABLE real_pack_stderr
    )
    if(NOT real_pack_result EQUAL 0 OR
       NOT real_pack_stderr STREQUAL "" OR
       NOT EXISTS "${real_carrier}")
        message(FATAL_ERROR
            "installed REVENGINE pack smoke failed\n"
            "exit: ${real_pack_result}\n"
            "stdout: ${real_pack_stdout}\nstderr: ${real_pack_stderr}")
    endif()
    string(JSON real_pack_schema GET "${real_pack_stdout}" schema)
    string(JSON real_pack_release GET "${real_pack_stdout}" release_identity)
    string(JSON real_pack_command GET "${real_pack_stdout}" command)
    string(JSON real_pack_ok GET "${real_pack_stdout}" ok)
    string(JSON real_pack_code GET "${real_pack_stdout}" code)
    string(JSON real_pack_exit GET "${real_pack_stdout}" exit_code)
    string(JSON real_pack_output GET "${real_pack_stdout}" result output_file)
    string(JSON real_pack_sha256 GET
        "${real_pack_stdout}" result container_sha256)
    if(NOT real_pack_schema STREQUAL "engine-sim-offline.cli-result.v1" OR
       NOT real_pack_release STREQUAL RELEASE_IDENTITY OR
       NOT real_pack_command STREQUAL "pack-revengine" OR
       NOT real_pack_ok OR NOT real_pack_code STREQUAL "success" OR
       NOT real_pack_exit EQUAL 0 OR
       NOT real_pack_output STREQUAL "${real_carrier}" OR
       NOT real_pack_sha256 MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR
            "installed REVENGINE pack machine result differs")
    endif()

    execute_process(
        COMMAND
            "${cli}" pack-revengine
            --package-directory "${repeat_bake_output}"
            --output "${repeat_carrier}"
            --result-format json
        RESULT_VARIABLE repeat_pack_result
        OUTPUT_VARIABLE repeat_pack_stdout
        ERROR_VARIABLE repeat_pack_stderr
    )
    if(NOT repeat_pack_result EQUAL 0 OR
       NOT repeat_pack_stderr STREQUAL "" OR
       NOT EXISTS "${repeat_carrier}")
        message(FATAL_ERROR
            "repeated installed REVENGINE pack failed\n"
            "exit: ${repeat_pack_result}\n"
            "stdout: ${repeat_pack_stdout}\n"
            "stderr: ${repeat_pack_stderr}")
    endif()
    string(JSON repeat_pack_release GET
        "${repeat_pack_stdout}" release_identity)
    string(JSON repeat_pack_sha256 GET
        "${repeat_pack_stdout}" result container_sha256)
    file(SIZE "${real_carrier}" real_carrier_size)
    file(SIZE "${repeat_carrier}" repeat_carrier_size)
    file(SHA256 "${real_carrier}" real_carrier_file_sha256)
    file(SHA256 "${repeat_carrier}" repeat_carrier_file_sha256)
    if(NOT repeat_pack_release STREQUAL RELEASE_IDENTITY OR
       NOT repeat_pack_sha256 STREQUAL real_pack_sha256 OR
       NOT real_carrier_size EQUAL repeat_carrier_size OR
       NOT real_carrier_file_sha256 STREQUAL repeat_carrier_file_sha256 OR
       NOT real_carrier_file_sha256 STREQUAL real_pack_sha256)
        message(FATAL_ERROR
            "repeated installed REVENGINE carrier bytes differ")
    endif()

    execute_process(
        COMMAND
            "${cli}" verify-revengine --input "${real_carrier}"
            --result-format json
        RESULT_VARIABLE real_verify_result
        OUTPUT_VARIABLE real_verify_stdout
        ERROR_VARIABLE real_verify_stderr
    )
    if(NOT real_verify_result EQUAL 0 OR
       NOT real_verify_stderr STREQUAL "")
        message(FATAL_ERROR
            "installed REVENGINE verify smoke failed\n"
            "exit: ${real_verify_result}\n"
            "stdout: ${real_verify_stdout}\nstderr: ${real_verify_stderr}")
    endif()
    string(JSON real_verify_schema GET "${real_verify_stdout}" schema)
    string(JSON real_verify_release GET
        "${real_verify_stdout}" release_identity)
    string(JSON real_verify_command GET "${real_verify_stdout}" command)
    string(JSON real_verify_ok GET "${real_verify_stdout}" ok)
    string(JSON real_verify_code GET "${real_verify_stdout}" code)
    string(JSON real_verify_exit GET "${real_verify_stdout}" exit_code)
    string(JSON real_verify_verified GET
        "${real_verify_stdout}" result verified)
    if(NOT real_verify_schema STREQUAL "engine-sim-offline.cli-result.v1" OR
       NOT real_verify_release STREQUAL RELEASE_IDENTITY OR
       NOT real_verify_command STREQUAL "verify-revengine" OR
       NOT real_verify_ok OR NOT real_verify_code STREQUAL "success" OR
       NOT real_verify_exit EQUAL 0 OR NOT real_verify_verified)
        message(FATAL_ERROR
            "installed REVENGINE verify machine result differs")
    endif()

    execute_process(
        COMMAND
            "${NODE_EXECUTABLE}"
            "${SOURCE_ROOT}/tests/installed_revengine_playback_test.mjs"
            "${runtime_root}/revengine-audio-engine.js"
            "${real_carrier}"
        RESULT_VARIABLE installed_playback_result
        OUTPUT_VARIABLE installed_playback_stdout
        ERROR_VARIABLE installed_playback_stderr
        TIMEOUT 30
    )
    if(NOT installed_playback_result EQUAL 0 OR
       NOT installed_playback_stdout STREQUAL "" OR
       NOT installed_playback_stderr STREQUAL "")
        message(FATAL_ERROR
            "installed REVENGINE streaming playback gate failed\n"
            "exit: ${installed_playback_result}\n"
            "stdout: ${installed_playback_stdout}\n"
            "stderr: ${installed_playback_stderr}")
    endif()

    execute_process(
        COMMAND
            "${NODE_EXECUTABLE}" --input-type=module --eval
            "const {readFile}=await import('node:fs/promises'); const {webcrypto}=await import('node:crypto'); const {pathToFileURL}=await import('node:url'); const module=await import(pathToFileURL(process.argv[1]).href); const engine=await module.RevengineAudioEngine.load(await readFile(process.argv[2]),{crypto:webcrypto}); const pcm=engine.render(8192); let power=0; for(const sample of pcm){if(!Number.isFinite(sample)) process.exit(2); power+=sample*sample;} if(pcm.length!==8192||!(power>0)||engine.channelCount!==1) process.exit(2);"
            "${runtime_root}/revengine-audio-engine.js"
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
            "installed REVENGINE audio-bridge smoke failed\n"
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
