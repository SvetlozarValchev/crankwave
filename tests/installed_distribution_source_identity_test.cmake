cmake_minimum_required(VERSION 3.21)

foreach(_required IN ITEMS
        TEST_DIRECTORY
        GIT_EXECUTABLE
        SOURCE_IDENTITY_SCRIPT
        COMPLETE_DISTRIBUTION_SCRIPT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

set(source_root "${TEST_DIRECTORY}/source")
set(input_paths "${TEST_DIRECTORY}/release-source-inputs.txt")
file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY
    "${source_root}/src/cli"
    "${source_root}/web/runtime"
    "${source_root}/web/crankwave-harness")
file(WRITE "${source_root}/src/cli/bake_crankwave_command.cpp"
    "int bake_crankwave_command() { return 0; }\n")
file(WRITE "${source_root}/web/runtime/runtime.js"
    "export const runtime = true;\n")
file(WRITE "${source_root}/web/crankwave-harness/app.js"
    "export const harness = true;\n")
file(WRITE "${source_root}/user-notes.txt" "private notes\n")
file(WRITE "${input_paths}"
    "src/cli/bake_crankwave_command.cpp\nweb/runtime/runtime.js\n")

function(run_git)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${source_root}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_out
        ERROR_VARIABLE standard_error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "temporary Git command failed\n"
            "stdout: ${standard_out}\nstderr: ${standard_error}")
    endif()
endfunction()

function(read_source_identity output)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE_ROOT=${source_root}"
            "-DINPUT_PATHS_FILE=${input_paths}"
            "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
            -P "${SOURCE_IDENTITY_SCRIPT}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_out
        ERROR_VARIABLE standard_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT result EQUAL 0 OR NOT standard_error STREQUAL "")
        message(FATAL_ERROR
            "source identity command failed\n"
            "stdout: ${standard_out}\nstderr: ${standard_error}")
    endif()
    set(${output} "${standard_out}" PARENT_SCOPE)
endfunction()

run_git(init --quiet)
run_git(config user.name "Crankwave Test")
run_git(config user.email "crankwave-test@example.invalid")
run_git(add -- .)
run_git(commit --quiet -m initial)

read_source_identity(clean_identity)
string(JSON clean_state GET "${clean_identity}" state)
string(JSON clean_head GET "${clean_identity}" git_commit)
string(JSON clean_closure GET "${clean_identity}" closure_sha256)
if(NOT clean_state STREQUAL "clean" OR
   NOT clean_head MATCHES "^[0-9a-f]+$" OR
   NOT clean_closure MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "clean release source identity differs")
endif()

# The v2 completeness preflight is intentionally native-only. A canonical clean
# stamp and clean installed-input closure are sufficient; no Node or WASM variables
# are supplied to this invocation.
set(source_stamp "${TEST_DIRECTORY}/renderer-source-stamp.hpp")
file(WRITE "${source_stamp}"
    "inline constexpr auto kRendererSourceState = \"clean\";\n"
    "inline constexpr auto kRendererToolchainState = \"available\";\n"
    "inline constexpr auto kRendererFullGitHead = \"${clean_head}\";\n"
    "inline constexpr auto kRendererSourceClosureSha256 = \"${clean_closure}\";\n")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        "-DSOURCE_STAMP=${source_stamp}"
        "-DSOURCE_ROOT=${source_root}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DRELEASE_SOURCE_INPUTS=${input_paths}"
        "-DRELEASE_SOURCE_IDENTITY_SCRIPT=${SOURCE_IDENTITY_SCRIPT}"
        -P "${COMPLETE_DISTRIBUTION_SCRIPT}"
    RESULT_VARIABLE native_complete_result
    OUTPUT_VARIABLE native_complete_stdout
    ERROR_VARIABLE native_complete_stderr
)
if(NOT native_complete_result EQUAL 0 OR
   NOT native_complete_stdout STREQUAL "" OR
   NOT native_complete_stderr STREQUAL "")
    message(FATAL_ERROR
        "native-only distribution completeness preflight failed\n"
        "exit: ${native_complete_result}\n"
        "stdout: ${native_complete_stdout}\n"
        "stderr: ${native_complete_stderr}")
endif()

file(APPEND "${source_root}/web/crankwave-harness/app.js" "// local UI\n")
file(APPEND "${source_root}/user-notes.txt" "more private notes\n")
read_source_identity(unrelated_identity)
string(JSON unrelated_state GET "${unrelated_identity}" state)
string(JSON unrelated_closure GET
    "${unrelated_identity}" closure_sha256)
if(NOT unrelated_state STREQUAL "clean" OR
   NOT unrelated_closure STREQUAL clean_closure)
    message(FATAL_ERROR
        "unrelated source files contaminated the release identity")
endif()

file(APPEND "${source_root}/web/runtime/runtime.js" "// release change\n")
read_source_identity(dirty_identity)
string(JSON dirty_state GET "${dirty_identity}" state)
string(JSON dirty_closure GET "${dirty_identity}" closure_sha256)
if(NOT dirty_state STREQUAL "dirty" OR
   dirty_closure STREQUAL clean_closure)
    message(FATAL_ERROR
        "changed installed runtime did not dirty the release identity")
endif()

file(WRITE "${source_root}/web/runtime/runtime.js"
    "export const runtime = true;\n")
run_git(update-index --assume-unchanged
    src/cli/bake_crankwave_command.cpp)
file(APPEND "${source_root}/src/cli/bake_crankwave_command.cpp"
    "// hidden native release change\n")
read_source_identity(hidden_dirty_identity)
string(JSON hidden_dirty_state GET "${hidden_dirty_identity}" state)
string(JSON hidden_dirty_closure GET
    "${hidden_dirty_identity}" closure_sha256)
if(NOT hidden_dirty_state STREQUAL "dirty" OR
   hidden_dirty_closure STREQUAL clean_closure)
    message(FATAL_ERROR
        "assume-unchanged installed tool input did not dirty the release identity")
endif()
run_git(update-index --no-assume-unchanged
    src/cli/bake_crankwave_command.cpp)

# Release attachments count as clean only when their actual bytes match a clean,
# tracked manifest. Git ignore must not hide changes to an installed payload.
run_git(checkout -- src/cli/bake_crankwave_command.cpp web/runtime/runtime.js)
file(MAKE_DIRECTORY "${source_root}/assets")
file(WRITE "${source_root}/assets/runtime.bin" "original asset\n")
file(SHA256 "${source_root}/assets/runtime.bin" asset_sha)
file(SIZE "${source_root}/assets/runtime.bin" asset_size)
file(WRITE "${source_root}/.gitignore" "/assets/runtime.bin\n")
file(WRITE "${source_root}/source-assets.lock.json"
    "{\"version\":1,\"files\":[{\"path\":\"assets/runtime.bin\",\"size\":${asset_size},\"sha256\":\"${asset_sha}\"}]}\n")
file(WRITE "${input_paths}"
    "assets/runtime.bin\nsource-assets.lock.json\nsrc/cli/bake_crankwave_command.cpp\nweb/runtime/runtime.js\n")
run_git(add -- .gitignore source-assets.lock.json)
run_git(commit --quiet -m "pin release attachment")
read_source_identity(asset_identity)
string(JSON asset_state GET "${asset_identity}" state)
if(NOT asset_state STREQUAL "clean")
    message(FATAL_ERROR "verified attachment did not count as clean source")
endif()
file(WRITE "${source_root}/assets/runtime.bin" "modified asset\n")
read_source_identity(changed_asset_identity)
string(JSON changed_asset_state GET "${changed_asset_identity}" state)
if(NOT changed_asset_state STREQUAL "dirty")
    message(FATAL_ERROR "modified ignored attachment did not dirty source identity")
endif()
file(WRITE "${source_root}/assets/runtime.bin" "original asset\n")
file(APPEND "${source_root}/source-assets.lock.json" " \n")
read_source_identity(changed_lock_identity)
string(JSON changed_lock_state GET "${changed_lock_identity}" state)
if(NOT changed_lock_state STREQUAL "dirty")
    message(FATAL_ERROR "modified asset manifest did not dirty source identity")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
