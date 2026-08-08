cmake_minimum_required(VERSION 3.21)

foreach(required IN ITEMS
        BUILD_DIRECTORY
        INSTALL_PREFIX
        INSTALL_BINDIR
        INSTALL_DATADIR
        RELEASE_IDENTITY
        SOURCE_ROOT
        TAR_EXECUTABLE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
foreach(relative_directory IN ITEMS INSTALL_BINDIR INSTALL_DATADIR)
    if(IS_ABSOLUTE "${${relative_directory}}")
        message(FATAL_ERROR
            "installed-distribution smoke requires relative GNUInstallDirs paths")
    endif()
endforeach()

function(require_regular_file path)
    if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}" OR IS_SYMLINK "${path}")
        message(FATAL_ERROR "installed distribution file is not regular: ${path}")
    endif()
endfunction()

function(run_install prefix result_out stdout_out stderr_out)
    set(command "${CMAKE_COMMAND}" --install "${BUILD_DIRECTORY}" --prefix "${prefix}")
    if(DEFINED INSTALL_CONFIG AND NOT INSTALL_CONFIG STREQUAL "")
        list(APPEND command --config "${INSTALL_CONFIG}")
    endif()
    execute_process(
        COMMAND ${command}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_out
        ERROR_VARIABLE standard_error
    )
    set(${result_out} "${result}" PARENT_SCOPE)
    set(${stdout_out} "${standard_out}" PARENT_SCOPE)
    set(${stderr_out} "${standard_error}" PARENT_SCOPE)
endfunction()

set(staged_install_prefix "${INSTALL_PREFIX}.before-relocation")
file(REMOVE_RECURSE "${staged_install_prefix}" "${INSTALL_PREFIX}")
run_install("${staged_install_prefix}" install_result install_stdout install_stderr)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR
        "prefix install failed\nexit: ${install_result}\n"
        "stdout: ${install_stdout}\nstderr: ${install_stderr}")
endif()
file(RENAME "${staged_install_prefix}" "${INSTALL_PREFIX}")

set(bin_directory "${INSTALL_PREFIX}/${INSTALL_BINDIR}")
set(resource_relative
    "${INSTALL_DATADIR}/engine-sim-offline/${RELEASE_IDENTITY}")
set(resource_root "${INSTALL_PREFIX}/${resource_relative}")
set(runtime_root "${resource_root}/web/runtime")
set(cli "${bin_directory}/engine-sim-offline")
set(workflow_path
    "${resource_root}/contracts/revengine-bake-workflow.v2.json")
set(release_manifest_path "${resource_root}/release.json")

set(required_files
    "${cli}"
    "${resource_root}/package.json"
    "${resource_root}/licenses/ENGINE-SIM-OFFLINE.txt"
    "${resource_root}/licenses/THIRD-PARTY-NOTICES.md"
    "${release_manifest_path}"
    "${resource_root}/release.json.sha256"
    "${workflow_path}"
    "${resource_root}/docs/contracts/CLI_RESULT_V1.md"
    "${resource_root}/docs/contracts/INSTALLED_DISTRIBUTION_V2.md"
    "${resource_root}/docs/contracts/RESPONSIVE_PROFILE_SELECTION_V2.md"
    "${resource_root}/docs/contracts/IR_AUTHORING_CATALOG_V1.md"
    "${resource_root}/assets/catalog.v1.json"
    "${resource_root}/assets/ir-authoring-catalog.v1.json"
    "${resource_root}/assets/runtime-audio/shared-recorded-starter/runtime.json"
    "${resource_root}/assets/runtime-audio/shared-recorded-starter/audio/recorded-starter.cropped.192000hz.mono.f32le"
    "${resource_root}/profiles/interactive-preview-v1.json"
    "${resource_root}/schemas/responsive-audio-bake-profile.schema.json"
    "${resource_root}/schemas/ir-authoring-catalog.schema.json"
    "${resource_root}/schemas/installed-distribution.v2.schema.json"
    "${resource_root}/schemas/revengine-bake-workflow.v2.schema.json")
foreach(required_file IN LISTS required_files)
    require_regular_file("${required_file}")
endforeach()

file(READ "${resource_root}/docs/contracts/CLI_RESULT_V1.md"
    cli_result_contract)
foreach(required_contract_term IN ITEMS
        "bake-revengine"
        "cache_identity_sha256"
        "bake-revengine-deadline-exceeded"
        "native-responsive-*"
        "1.2.0")
    string(FIND "${cli_result_contract}" "${required_contract_term}"
        required_contract_term_offset)
    if(required_contract_term_offset EQUAL -1)
        message(FATAL_ERROR
            "installed CLI result contract omits ${required_contract_term}")
    endif()
endforeach()

# The browser closure is playback-only ESM. None of the old simulation C API or
# WASM heap adapters is a v2 production member.
file(GLOB runtime_entries RELATIVE "${runtime_root}" "${runtime_root}/*")
list(SORT runtime_entries)
set(expected_runtime_entries
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
    steady-transient-envelope.js)
if(NOT "${runtime_entries}" STREQUAL "${expected_runtime_entries}")
    message(FATAL_ERROR
        "installed simulator-free ESM closure differs: ${runtime_entries}")
endif()

file(GLOB_RECURSE installed_entries LIST_DIRECTORIES FALSE
    RELATIVE "${INSTALL_PREFIX}" "${INSTALL_PREFIX}/*")
list(SORT installed_entries)
foreach(installed_entry IN LISTS installed_entries)
    if(installed_entry MATCHES "(^|/)engine-sim-offline-responsive-bake$" OR
       installed_entry MATCHES "(^|/)dump-ir-spectrum(\\.cpp)?$" OR
       installed_entry MATCHES "(^|/)bake\\.mjs$" OR
       installed_entry MATCHES "(^|/)c-api-(abi|client|errors|session)\\.js$" OR
       installed_entry MATCHES "(^|/)wasm-heap\\.js$" OR
       installed_entry MATCHES "(^|/)engine-sim-offline\\.(js|wasm)$" OR
       installed_entry MATCHES "(^|/)tools/responsive-audio-baker(/|$)" OR
       installed_entry MATCHES "(^|/)renderer(/|$)")
        message(FATAL_ERROR
            "forbidden Node/WASM baker member was installed: ${installed_entry}")
    endif()
endforeach()

file(READ "${resource_root}/package.json" package_metadata)
string(JSON package_type GET "${package_metadata}" type)
string(JSON package_version GET "${package_metadata}" version)
if(NOT package_type STREQUAL "module" OR
   NOT package_version STREQUAL RELEASE_IDENTITY OR
   package_metadata MATCHES "\"engines\"[ \t\r\n]*:")
    message(FATAL_ERROR
        "installed ESM metadata declares an invalid identity or runtime dependency")
endif()
file(READ "${runtime_root}/release.js" javascript_release)
if(NOT javascript_release MATCHES
   "ENGINE_SIM_OFFLINE_RELEASE_IDENTITY[ \t\r\n]*=[ \t\r\n]*\"${RELEASE_IDENTITY}\"")
    message(FATAL_ERROR "installed ESM release identity differs")
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
    COMMAND "${cli}" --help
    RESULT_VARIABLE cli_help_result
    OUTPUT_VARIABLE cli_help_stdout
    ERROR_VARIABLE cli_help_stderr
)
foreach(command IN ITEMS
        render bake-revengine pack-revengine inspect-revengine
        verify-revengine inspect-ir-catalog)
    if(NOT cli_help_stdout MATCHES "${command}")
        message(FATAL_ERROR "installed CLI help omits ${command}")
    endif()
endforeach()
if(NOT cli_help_result EQUAL 0 OR NOT cli_help_stderr STREQUAL "")
    message(FATAL_ERROR "installed CLI help failed")
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
   NOT ir_result_ok OR NOT ir_result_count EQUAL 73 OR
   NOT ir_result_sha STREQUAL installed_ir_catalog_sha OR
   NOT ir_catalog_schema STREQUAL
       "engine-sim-offline/ir-authoring-catalog.v1" OR
   NOT ir_catalog_release STREQUAL RELEASE_IDENTITY OR
   NOT ir_catalog_entry_count EQUAL 73)
    message(FATAL_ERROR "installed IR authoring release binding differs")
endif()
math(EXPR ir_catalog_last "${ir_catalog_entry_count} - 1")
foreach(ir_catalog_index RANGE 0 ${ir_catalog_last})
    string(JSON ir_payload_sha GET "${ir_catalog_stdout}"
        result catalog entries ${ir_catalog_index} sha256)
    set(ir_payload "${resource_root}/assets/payloads/${ir_payload_sha}")
    require_regular_file("${ir_payload}")
    file(SHA256 "${ir_payload}" ir_payload_actual_sha)
    if(NOT ir_payload_actual_sha STREQUAL ir_payload_sha)
        message(FATAL_ERROR
            "installed IR catalog payload hash differs: ${ir_payload_sha}")
    endif()
endforeach()

file(READ "${resource_root}/profiles/interactive-preview-v1.json" profile)
string(JSON profile_schema GET "${profile}" schema)
string(JSON profile_id GET "${profile}" id)
string(JSON profile_anchor_count LENGTH "${profile}" rpm anchors)
if(NOT profile_schema STREQUAL
       "engine-sim-offline/responsive-audio-bake-profile-v1" OR
   NOT profile_id STREQUAL "interactive-preview-v1" OR
   NOT profile_anchor_count EQUAL 11)
    message(FATAL_ERROR "installed responsive profile resource differs")
endif()

file(READ "${workflow_path}" workflow)
string(JSON workflow_schema GET "${workflow}" schema)
string(JSON workflow_release GET "${workflow}" release_identity)
string(JSON workflow_step_count LENGTH "${workflow}" steps)
string(JSON workflow_runtime_kind GET "${workflow}" production_runtime kind)
string(JSON workflow_node_required GET
    "${workflow}" production_runtime node_required)
string(JSON workflow_wasm_required GET
    "${workflow}" production_runtime simulation_wasm_required)
string(JSON workflow_profile_policy GET
    "${workflow}" responsive_profile_selection default_policy)
string(JSON workflow_profile_id GET
    "${workflow}" responsive_profile_selection profile_id)
string(JSON workflow_profile_override_type TYPE
    "${workflow}" responsive_profile_selection explicit_override)
if(NOT workflow_schema STREQUAL
       "engine-sim-offline/revengine-bake-workflow.v2" OR
   NOT workflow_release STREQUAL RELEASE_IDENTITY OR
   NOT workflow_step_count EQUAL 1 OR
   NOT workflow_runtime_kind STREQUAL "native" OR
   workflow_node_required OR workflow_wasm_required OR
   NOT workflow_profile_policy STREQUAL "engine-redline-affine-v1" OR
   NOT workflow_profile_id STREQUAL "interactive-preview-redline-v1" OR
   NOT workflow_profile_override_type STREQUAL "NULL")
    message(FATAL_ERROR "installed native bake workflow identity differs")
endif()
string(JSON workflow_step_ordinal GET "${workflow}" steps 0 ordinal)
string(JSON workflow_step_id GET "${workflow}" steps 0 id)
string(JSON workflow_step_executable GET "${workflow}" steps 0 executable)
string(JSON workflow_argument_count LENGTH "${workflow}" steps 0 arguments)
set(workflow_arguments)
math(EXPR workflow_argument_last "${workflow_argument_count} - 1")
foreach(workflow_argument_index RANGE 0 ${workflow_argument_last})
    string(JSON workflow_argument GET "${workflow}"
        steps 0 arguments ${workflow_argument_index})
    list(APPEND workflow_arguments "${workflow_argument}")
endforeach()
set(expected_workflow_arguments
    bake-revengine
    --engine
    {engine_json}
    --output
    {new_revengine_file}
    --deadline-unix-ms
    {deadline_unix_ms}
    --result-format
    json)
if(NOT workflow_step_ordinal EQUAL 1 OR
   NOT workflow_step_id STREQUAL "bake_revengine" OR
   NOT workflow_step_executable STREQUAL
       "${INSTALL_BINDIR}/engine-sim-offline" OR
   NOT "${workflow_arguments}" STREQUAL "${expected_workflow_arguments}")
    message(FATAL_ERROR "installed native bake command sequence differs")
endif()
string(JSON workflow_result_schema GET
    "${workflow}" steps 0 required_stdout_record schema)
string(JSON workflow_result_release GET
    "${workflow}" steps 0 required_stdout_record release_identity)
string(JSON workflow_result_command GET
    "${workflow}" steps 0 required_stdout_record command)
string(JSON workflow_result_ok GET
    "${workflow}" steps 0 required_stdout_record ok)
string(JSON workflow_result_verified GET
    "${workflow}" steps 0 required_result_fields verified)
if(NOT workflow_result_schema STREQUAL "engine-sim-offline.cli-result.v1" OR
   NOT workflow_result_release STREQUAL RELEASE_IDENTITY OR
   NOT workflow_result_command STREQUAL "bake-revengine" OR
   NOT workflow_result_ok OR NOT workflow_result_verified)
    message(FATAL_ERROR "installed native bake result contract differs")
endif()
string(JSON workflow_telemetry_role GET
    "${workflow}" telemetry_contract role)
string(JSON workflow_telemetry_schema GET
    "${workflow}" telemetry_contract schema)
string(JSON workflow_telemetry_commit GET
    "${workflow}" telemetry_contract originating_commit)
if(NOT workflow_telemetry_role STREQUAL "diagnostics.engine-telemetry.v1" OR
   NOT workflow_telemetry_schema STREQUAL
       "engine-sim-offline.engine-telemetry.ndjson.v1" OR
   NOT workflow_telemetry_commit STREQUAL
       "c8d672b59e3046654ad5f818f31725798aef7ffa")
    message(FATAL_ERROR "installed workflow telemetry boundary differs")
endif()

file(READ "${release_manifest_path}" release_manifest)
file(READ "${resource_root}/release.json.sha256" release_binding)
file(SHA256 "${release_manifest_path}" actual_release_binding)
if(NOT release_binding STREQUAL "${actual_release_binding}\n")
    message(FATAL_ERROR "installed distribution binding digest differs")
endif()
string(JSON release_schema GET "${release_manifest}" schema)
string(JSON release_identity GET "${release_manifest}" release_identity)
string(JSON release_classification GET "${release_manifest}" classification)
string(JSON release_complete GET "${release_manifest}" complete)
string(JSON release_runtime_kind GET
    "${release_manifest}" production_runtime kind)
string(JSON release_node_required GET
    "${release_manifest}" production_runtime node_required)
string(JSON release_wasm_required GET
    "${release_manifest}" production_runtime simulation_wasm_required)
string(JSON release_native_path GET "${release_manifest}" native_cli path)
string(JSON release_native_sha256 GET "${release_manifest}" native_cli sha256)
string(JSON release_command_count LENGTH
    "${release_manifest}" native_cli commands)
string(JSON release_browser_kind GET
    "${release_manifest}" browser_playback kind)
string(JSON release_browser_directory GET
    "${release_manifest}" browser_playback resource_directory)
string(JSON release_browser_entrypoint GET
    "${release_manifest}" browser_playback entrypoint)
string(JSON release_resource_root GET
    "${release_manifest}" layout resource_root)
string(JSON release_telemetry_role GET
    "${release_manifest}" telemetry_contract role)
string(JSON release_telemetry_schema GET
    "${release_manifest}" telemetry_contract schema)
string(JSON release_telemetry_commit GET
    "${release_manifest}" telemetry_contract originating_commit)
file(SHA256 "${cli}" actual_native_sha256)
if(NOT release_schema STREQUAL
       "engine-sim-offline/installed-distribution.v2" OR
   NOT release_identity STREQUAL RELEASE_IDENTITY OR
   NOT release_runtime_kind STREQUAL "native" OR
   release_node_required OR release_wasm_required OR
   NOT release_native_path STREQUAL "${INSTALL_BINDIR}/engine-sim-offline" OR
   NOT release_native_sha256 STREQUAL actual_native_sha256 OR
   NOT release_command_count EQUAL 6 OR
   NOT release_browser_kind STREQUAL "simulator-free-esm" OR
   NOT release_browser_directory STREQUAL "${resource_relative}/web/runtime" OR
   NOT release_browser_entrypoint STREQUAL
       "${resource_relative}/web/runtime/revengine-audio-engine.js" OR
   NOT release_resource_root STREQUAL resource_relative OR
   NOT release_telemetry_role STREQUAL "diagnostics.engine-telemetry.v1" OR
   NOT release_telemetry_schema STREQUAL
       "engine-sim-offline.engine-telemetry.ndjson.v1" OR
   NOT release_telemetry_commit STREQUAL
       "c8d672b59e3046654ad5f818f31725798aef7ffa")
    message(FATAL_ERROR "installed distribution v2 metadata differs")
endif()
set(expected_commands
    render bake-revengine pack-revengine inspect-revengine
    verify-revengine inspect-ir-catalog)
set(release_commands)
foreach(command_index RANGE 0 5)
    string(JSON release_command GET
        "${release_manifest}" native_cli commands ${command_index})
    list(APPEND release_commands "${release_command}")
endforeach()
if(NOT "${release_commands}" STREQUAL "${expected_commands}")
    message(FATAL_ERROR "installed manifest command closure differs")
endif()
if(release_complete)
    if(NOT release_classification STREQUAL "immutable_release")
        message(FATAL_ERROR "complete installed distribution is misclassified")
    endif()
elseif(NOT release_classification STREQUAL
       "incomplete_development_install")
    message(FATAL_ERROR "development install masquerades as an immutable release")
endif()
file(SHA256 "${workflow_path}" actual_workflow_sha256)
string(JSON release_workflow_path GET
    "${release_manifest}" revengine_bake_workflow path)
string(JSON release_workflow_sha256 GET
    "${release_manifest}" revengine_bake_workflow sha256)
if(NOT release_workflow_path STREQUAL
       "${resource_relative}/contracts/revengine-bake-workflow.v2.json" OR
   NOT release_workflow_sha256 STREQUAL actual_workflow_sha256)
    message(FATAL_ERROR "installed workflow manifest binding differs")
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
        message(FATAL_ERROR "installed distribution closure is not ordered")
    endif()
    set(prior_release_file "${release_file}")
    list(APPEND manifest_release_files "${release_file}")
    set(release_file_path "${INSTALL_PREFIX}/${release_file}")
    require_regular_file("${release_file_path}")
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
list(APPEND actual_resource_files "${cli}")
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
    message(FATAL_ERROR "installed manifest omits or invents a member")
endif()

# Reinstall at a different absolute path. Generated metadata and the complete
# manifest-bound closure must remain byte-identical after relocation.
get_filename_component(install_prefix_name "${INSTALL_PREFIX}" NAME)
set(repeat_install_parent "${INSTALL_PREFIX}.repeat-parent")
set(repeat_install_prefix "${repeat_install_parent}/${install_prefix_name}")
file(REMOVE_RECURSE "${repeat_install_parent}")
run_install("${repeat_install_prefix}"
    repeat_install_result repeat_install_stdout repeat_install_stderr)
set(repeat_manifest
    "${repeat_install_prefix}/${resource_relative}/release.json")
if(NOT repeat_install_result EQUAL 0 OR NOT EXISTS "${repeat_manifest}")
    message(FATAL_ERROR
        "repeated prefix install failed\nexit: ${repeat_install_result}\n"
        "stdout: ${repeat_install_stdout}\nstderr: ${repeat_install_stderr}")
endif()
file(SHA256 "${repeat_manifest}" repeat_release_binding)
if(NOT repeat_release_binding STREQUAL actual_release_binding)
    message(FATAL_ERROR "relocated install produced a different release manifest")
endif()

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
        "-DRESOURCE_RELATIVE_DIRECTORY=${resource_relative}"
        "-DRELEASE_IDENTITY=${RELEASE_IDENTITY}"
        "-DOUTPUT_ARCHIVE=${first_archive}"
        "-DTAR_EXECUTABLE=${TAR_EXECUTABLE}"
        -DREQUIRE_COMPLETE=FALSE
        -P "${SOURCE_ROOT}/cmake/CreateDeterministicDistributionArchive.cmake"
    RESULT_VARIABLE first_archive_result
    OUTPUT_VARIABLE first_archive_stdout
    ERROR_VARIABLE first_archive_stderr
)
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        "-DPREFIX_DIRECTORY=${repeat_install_prefix}"
        "-DRESOURCE_RELATIVE_DIRECTORY=${resource_relative}"
        "-DRELEASE_IDENTITY=${RELEASE_IDENTITY}"
        "-DOUTPUT_ARCHIVE=${second_archive}"
        "-DTAR_EXECUTABLE=${TAR_EXECUTABLE}"
        -DREQUIRE_COMPLETE=FALSE
        -P "${SOURCE_ROOT}/cmake/CreateDeterministicDistributionArchive.cmake"
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
        "deterministic archive assembly failed\n"
        "first: ${first_archive_result} ${first_archive_stderr}\n"
        "second: ${second_archive_result} ${second_archive_stderr}")
endif()
file(SHA256 "${first_archive}" first_archive_sha256)
file(SHA256 "${second_archive}" second_archive_sha256)
if(NOT first_archive_sha256 STREQUAL second_archive_sha256)
    message(FATAL_ERROR "relocated distribution archive bytes differ")
endif()
execute_process(
    COMMAND "${TAR_EXECUTABLE}" -tf "${first_archive}"
    RESULT_VARIABLE archive_list_result
    OUTPUT_VARIABLE archive_list_stdout
    ERROR_VARIABLE archive_list_stderr
)
if(NOT archive_list_result EQUAL 0 OR
   NOT archive_list_stderr STREQUAL "" OR
   archive_list_stdout MATCHES "unmanifested-archive-probe")
    message(FATAL_ERROR "archive admitted debris outside the manifest closure")
endif()

set(tampered_member
    "${repeat_install_prefix}/${resource_relative}/contracts/revengine-bake-workflow.v2.json")
file(APPEND "${tampered_member}" " ")
set(tampered_archive "${INSTALL_PREFIX}.tampered.tar")
file(REMOVE "${tampered_archive}" "${tampered_archive}.sha256")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        "-DPREFIX_DIRECTORY=${repeat_install_prefix}"
        "-DRESOURCE_RELATIVE_DIRECTORY=${resource_relative}"
        "-DRELEASE_IDENTITY=${RELEASE_IDENTITY}"
        "-DOUTPUT_ARCHIVE=${tampered_archive}"
        "-DTAR_EXECUTABLE=${TAR_EXECUTABLE}"
        -DREQUIRE_COMPLETE=FALSE
        -P "${SOURCE_ROOT}/cmake/CreateDeterministicDistributionArchive.cmake"
    RESULT_VARIABLE tampered_archive_result
    OUTPUT_VARIABLE tampered_archive_stdout
    ERROR_VARIABLE tampered_archive_stderr
)
if(tampered_archive_result EQUAL 0 OR
   NOT tampered_archive_stdout STREQUAL "" OR
   NOT tampered_archive_stderr MATCHES "distribution member identity differs" OR
   EXISTS "${tampered_archive}" OR EXISTS "${tampered_archive}.sha256")
    message(FATAL_ERROR "tampered installed distribution did not fail closed")
endif()

# Exercise the relocated native cooker with an exact built-in long IR. This variant
# is faster than the legacy fixed-FFT IR while still proving extended native
# conversion, all responsive children, packing, verification, and publication.
set(work_root "${INSTALL_PREFIX}.native-work")
file(REMOVE_RECURSE "${work_root}")
file(MAKE_DIRECTORY "${work_root}" "${work_root}/empty-path")
file(READ "${SOURCE_ROOT}/data/engines/kohler-ch750-cleanroom/engine.json"
    bake_engine_json)
string(REPLACE "smooth-39" "smooth-45" bake_engine_json "${bake_engine_json}")
string(REPLACE "smooth_39.wav" "smooth_45.wav"
    bake_engine_json "${bake_engine_json}")
string(REPLACE
    "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc"
    "9da950e21499604aa100b791cfe6a21a192c6b8da26e7e91515b024a9b682819"
    bake_engine_json "${bake_engine_json}")
if(bake_engine_json MATCHES "smooth-39|smooth_39|75de9db4")
    message(FATAL_ERROR "installed native bake fixture rewrite was incomplete")
endif()
set(bake_engine "${work_root}/kohler-smooth-45.engine.json")
file(WRITE "${bake_engine}" "${bake_engine_json}")
set(deadline_output "${work_root}/deadline.revengine")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "PATH=${work_root}/empty-path"
        "AWS_ACCESS_KEY_ID=must-not-be-consumed"
        "AWS_SECRET_ACCESS_KEY=must-not-be-consumed"
        "HTTP_PROXY=http://127.0.0.1:1"
        "HTTPS_PROXY=http://127.0.0.1:1"
        "NODE_OPTIONS=must-not-be-consumed"
        "${cli}" bake-revengine
        --engine "${bake_engine}"
        --output "${deadline_output}"
        --deadline-unix-ms 1
        --result-format json
    RESULT_VARIABLE deadline_result
    OUTPUT_VARIABLE deadline_stdout
    ERROR_VARIABLE deadline_stderr
)
if(NOT deadline_result EQUAL 75 OR
   NOT deadline_stderr STREQUAL "" OR EXISTS "${deadline_output}")
    message(FATAL_ERROR
        "installed native deadline contract failed\n"
        "exit: ${deadline_result}\nstdout: ${deadline_stdout}\n"
        "stderr: ${deadline_stderr}")
endif()
string(JSON deadline_schema GET "${deadline_stdout}" schema)
string(JSON deadline_release GET "${deadline_stdout}" release_identity)
string(JSON deadline_command GET "${deadline_stdout}" command)
string(JSON deadline_ok GET "${deadline_stdout}" ok)
string(JSON deadline_code GET "${deadline_stdout}" code)
if(NOT deadline_schema STREQUAL "engine-sim-offline.cli-result.v1" OR
   NOT deadline_release STREQUAL RELEASE_IDENTITY OR
   NOT deadline_command STREQUAL "bake-revengine" OR deadline_ok OR
   NOT deadline_code STREQUAL "bake-revengine-deadline-exceeded")
    message(FATAL_ERROR "installed native deadline JSON differs")
endif()

if(release_complete)
find_program(bash_executable NAMES bash REQUIRED)
# A release must prefer its manifest-bound installed asset catalog even when
# caller debris mimics the build-tree sibling layout.
set(adjacent_poison_root "${bin_directory}/engine-sim-offline-assets")
file(MAKE_DIRECTORY "${adjacent_poison_root}")
file(WRITE "${adjacent_poison_root}/catalog.v1.json"
    "{\"poisoned_unmanifested_catalog\":true}\n")
set(signal_output "${work_root}/terminated.revengine")
set(signal_stdout_path "${work_root}/termination.stdout")
set(signal_stderr_path "${work_root}/termination.stderr")
execute_process(
    COMMAND
        "${bash_executable}"
        "${SOURCE_ROOT}/tests/installed_bake_signal_process_test.sh"
        "${cli}" "${bake_engine}" "${signal_output}"
        "${signal_stdout_path}" "${signal_stderr_path}" TERM
    RESULT_VARIABLE signal_result
    OUTPUT_VARIABLE signal_driver_stdout
    ERROR_VARIABLE signal_driver_stderr
    TIMEOUT 30
)
file(READ "${signal_stdout_path}" signal_stdout)
file(READ "${signal_stderr_path}" signal_stderr)
if(NOT signal_result EQUAL 75 OR
   NOT signal_driver_stdout STREQUAL "" OR
   NOT signal_driver_stderr STREQUAL "" OR
   NOT signal_stderr STREQUAL "" OR EXISTS "${signal_output}")
    message(FATAL_ERROR
        "installed native termination contract failed\n"
        "exit: ${signal_result}\nstdout: ${signal_stdout}\n"
        "stderr: ${signal_stderr}")
endif()
string(JSON signal_schema GET "${signal_stdout}" schema)
string(JSON signal_release GET "${signal_stdout}" release_identity)
string(JSON signal_command GET "${signal_stdout}" command)
string(JSON signal_ok GET "${signal_stdout}" ok)
string(JSON signal_code GET "${signal_stdout}" code)
if(NOT signal_schema STREQUAL "engine-sim-offline.cli-result.v1" OR
   NOT signal_release STREQUAL RELEASE_IDENTITY OR
   NOT signal_command STREQUAL "bake-revengine" OR signal_ok OR
   NOT signal_code STREQUAL "bake-revengine-terminated")
    message(FATAL_ERROR "installed native termination JSON differs")
endif()
file(GLOB interrupted_debris "${work_root}/.engine-sim-offline-*")
if(interrupted_debris)
    message(FATAL_ERROR
        "installed native cancellation left incomplete output: ${interrupted_debris}")
endif()

set(first_carrier "${work_root}/first.revengine")
set(second_carrier "${work_root}/second.revengine")
set(native_bake_environment
    "${CMAKE_COMMAND}" -E env
    "PATH=${work_root}/empty-path"
    "AWS_ACCESS_KEY_ID=must-not-be-consumed"
    "AWS_SECRET_ACCESS_KEY=must-not-be-consumed"
    "HTTP_PROXY=http://127.0.0.1:1"
    "HTTPS_PROXY=http://127.0.0.1:1"
    "NODE_OPTIONS=must-not-be-consumed")
execute_process(
    COMMAND
        ${native_bake_environment}
        "${cli}" bake-revengine
        --engine "${bake_engine}"
        --output "${first_carrier}"
        --result-format json
    RESULT_VARIABLE first_bake_result
    OUTPUT_VARIABLE first_bake_stdout
    ERROR_VARIABLE first_bake_stderr
    TIMEOUT 150
)
if(NOT first_bake_result EQUAL 0 OR
   NOT first_bake_stderr STREQUAL "" OR NOT EXISTS "${first_carrier}")
    message(FATAL_ERROR
        "installed native bake failed\nexit: ${first_bake_result}\n"
        "stdout: ${first_bake_stdout}\nstderr: ${first_bake_stderr}")
endif()
execute_process(
    COMMAND
        ${native_bake_environment}
        "${cli}" bake-revengine
        --engine "${bake_engine}"
        --output "${second_carrier}"
        --result-format json
    RESULT_VARIABLE second_bake_result
    OUTPUT_VARIABLE second_bake_stdout
    ERROR_VARIABLE second_bake_stderr
    TIMEOUT 150
)
if(NOT second_bake_result EQUAL 0 OR
   NOT second_bake_stderr STREQUAL "" OR NOT EXISTS "${second_carrier}")
    message(FATAL_ERROR
        "repeated installed native bake failed\nexit: ${second_bake_result}\n"
        "stdout: ${second_bake_stdout}\nstderr: ${second_bake_stderr}")
endif()
foreach(bake_stdout IN ITEMS first_bake_stdout second_bake_stdout)
    string(JSON bake_schema GET "${${bake_stdout}}" schema)
    string(JSON bake_release GET "${${bake_stdout}}" release_identity)
    string(JSON bake_command GET "${${bake_stdout}}" command)
    string(JSON bake_ok GET "${${bake_stdout}}" ok)
    string(JSON bake_verified GET "${${bake_stdout}}" result verified)
    string(JSON bake_profile GET "${${bake_stdout}}" result profile_id)
    if(NOT bake_schema STREQUAL "engine-sim-offline.cli-result.v1" OR
       NOT bake_release STREQUAL RELEASE_IDENTITY OR
       NOT bake_command STREQUAL "bake-revengine" OR
       NOT bake_ok OR NOT bake_verified OR
       NOT bake_profile STREQUAL "interactive-preview-redline-v1")
        message(FATAL_ERROR "installed native bake result identity differs")
    endif()
endforeach()
file(SIZE "${first_carrier}" first_carrier_bytes)
file(SIZE "${second_carrier}" second_carrier_bytes)
file(SHA256 "${first_carrier}" first_carrier_sha256)
file(SHA256 "${second_carrier}" second_carrier_sha256)
if(first_carrier_bytes LESS 1 OR
   NOT first_carrier_bytes EQUAL second_carrier_bytes OR
   NOT first_carrier_sha256 STREQUAL second_carrier_sha256)
    message(FATAL_ERROR "repeated relocated native carrier bytes differ")
endif()

execute_process(
    COMMAND "${cli}" inspect-revengine --input "${first_carrier}"
        --result-format json
    RESULT_VARIABLE inspect_result
    OUTPUT_VARIABLE inspect_stdout
    ERROR_VARIABLE inspect_stderr
)
execute_process(
    COMMAND "${cli}" verify-revengine --input "${first_carrier}"
        --result-format json
    RESULT_VARIABLE verify_result
    OUTPUT_VARIABLE verify_stdout
    ERROR_VARIABLE verify_stderr
)
if(NOT inspect_result EQUAL 0 OR NOT inspect_stderr STREQUAL "" OR
   NOT verify_result EQUAL 0 OR NOT verify_stderr STREQUAL "")
    message(FATAL_ERROR "installed native carrier inspection/verification failed")
endif()
string(JSON inspect_release GET "${inspect_stdout}" release_identity)
string(JSON inspect_command GET "${inspect_stdout}" command)
string(JSON verify_release GET "${verify_stdout}" release_identity)
string(JSON verify_command GET "${verify_stdout}" command)
string(JSON verify_verified GET "${verify_stdout}" result verified)
if(NOT inspect_release STREQUAL RELEASE_IDENTITY OR
   NOT inspect_command STREQUAL "inspect-revengine" OR
   NOT verify_release STREQUAL RELEASE_IDENTITY OR
   NOT verify_command STREQUAL "verify-revengine" OR NOT verify_verified)
    message(FATAL_ERROR "installed native inspect/verify result identity differs")
endif()

if(DEFINED NODE_EXECUTABLE AND NOT NODE_EXECUTABLE STREQUAL "" AND
   EXISTS "${NODE_EXECUTABLE}" AND
   EXISTS "${SOURCE_ROOT}/tests/installed_revengine_playback_test.mjs")
    execute_process(
        COMMAND
            "${NODE_EXECUTABLE}"
            "${SOURCE_ROOT}/tests/installed_revengine_playback_test.mjs"
            "${runtime_root}/revengine-audio-engine.js"
            "${first_carrier}"
        RESULT_VARIABLE playback_result
        OUTPUT_VARIABLE playback_stdout
        ERROR_VARIABLE playback_stderr
        TIMEOUT 30
    )
    if(NOT playback_result EQUAL 0 OR
       NOT playback_stdout STREQUAL "" OR
       NOT playback_stderr STREQUAL "")
        message(FATAL_ERROR
            "relocated installed playback failed\n"
            "exit: ${playback_result}\nstdout: ${playback_stdout}\n"
            "stderr: ${playback_stderr}")
    endif()
endif()

set(tampered_carrier "${work_root}/tampered.revengine")
file(COPY_FILE "${first_carrier}" "${tampered_carrier}")
file(APPEND "${tampered_carrier}" "tamper")
execute_process(
    COMMAND "${cli}" verify-revengine --input "${tampered_carrier}"
        --result-format json
    RESULT_VARIABLE tampered_verify_result
    OUTPUT_VARIABLE tampered_verify_stdout
    ERROR_VARIABLE tampered_verify_stderr
)
if(tampered_verify_result EQUAL 0 OR
   NOT tampered_verify_stderr STREQUAL "")
    message(FATAL_ERROR "tampered native carrier did not fail verification")
endif()
string(JSON tampered_verify_schema GET "${tampered_verify_stdout}" schema)
string(JSON tampered_verify_release GET
    "${tampered_verify_stdout}" release_identity)
string(JSON tampered_verify_command GET "${tampered_verify_stdout}" command)
string(JSON tampered_verify_ok GET "${tampered_verify_stdout}" ok)
if(NOT tampered_verify_schema STREQUAL "engine-sim-offline.cli-result.v1" OR
   NOT tampered_verify_release STREQUAL RELEASE_IDENTITY OR
   NOT tampered_verify_command STREQUAL "verify-revengine" OR
   tampered_verify_ok)
    message(FATAL_ERROR "tampered native carrier failure envelope differs")
endif()
file(REMOVE_RECURSE "${adjacent_poison_root}")
else()
    # A dirty development binary deliberately refuses to mint a carrier because its
    # backend identity is not publishable. The complete clean-tree release gate above
    # exercises termination, repeat cooking, verification, and carrier tamper checks.
    set(incomplete_output "${work_root}/incomplete.revengine")
    execute_process(
        COMMAND "${cli}" bake-revengine
            --engine "${bake_engine}"
            --output "${incomplete_output}"
            --result-format json
        RESULT_VARIABLE incomplete_bake_result
        OUTPUT_VARIABLE incomplete_bake_stdout
        ERROR_VARIABLE incomplete_bake_stderr
    )
    if(NOT incomplete_bake_result EQUAL 69 OR
       NOT incomplete_bake_stderr STREQUAL "" OR
       EXISTS "${incomplete_output}")
        message(FATAL_ERROR
            "incomplete installed cooker did not fail closed")
    endif()
    string(JSON incomplete_bake_code GET "${incomplete_bake_stdout}" code)
    if(NOT incomplete_bake_code STREQUAL
       "native-responsive-backend-identity-unavailable")
        message(FATAL_ERROR
            "incomplete installed cooker failure identity differs")
    endif()
endif()

file(REMOVE "${unmanifested_archive_probe}")
file(REMOVE_RECURSE "${repeat_install_parent}" "${work_root}")
file(REMOVE
    "${first_archive}" "${first_archive}.sha256"
    "${second_archive}" "${second_archive}.sha256"
    "${tampered_archive}" "${tampered_archive}.sha256")
