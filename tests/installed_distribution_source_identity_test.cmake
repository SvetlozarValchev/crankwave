cmake_minimum_required(VERSION 3.21)

foreach(_required IN ITEMS TEST_DIRECTORY GIT_EXECUTABLE SOURCE_IDENTITY_SCRIPT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

set(source_root "${TEST_DIRECTORY}/source")
set(input_paths "${TEST_DIRECTORY}/release-source-inputs.txt")
file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY
    "${source_root}/tools/responsive-audio-baker"
    "${source_root}/web/runtime"
    "${source_root}/web/revengine-harness")
file(WRITE "${source_root}/tools/responsive-audio-baker/bake.mjs"
    "export const bake = true;\n")
file(WRITE "${source_root}/web/runtime/runtime.js"
    "export const runtime = true;\n")
file(WRITE "${source_root}/web/revengine-harness/app.js"
    "export const harness = true;\n")
file(WRITE "${source_root}/user-notes.txt" "private notes\n")
file(WRITE "${input_paths}"
    "tools/responsive-audio-baker/bake.mjs\nweb/runtime/runtime.js\n")

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
run_git(config user.name "Engine Sim Offline Test")
run_git(config user.email "engine-sim-offline-test@example.invalid")
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

file(APPEND "${source_root}/web/revengine-harness/app.js" "// local UI\n")
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
    tools/responsive-audio-baker/bake.mjs)
file(APPEND "${source_root}/tools/responsive-audio-baker/bake.mjs"
    "// hidden release change\n")
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
    tools/responsive-audio-baker/bake.mjs)

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
