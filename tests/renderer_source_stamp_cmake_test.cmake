cmake_minimum_required(VERSION 3.21)

foreach(
    _required
    IN ITEMS
        GENERATOR_SCRIPT
        TEST_ROOT
        GIT_EXECUTABLE
        TEST_COMPILER_EXECUTABLE
        TEST_COMPILER_ID
        TEST_COMPILER_VERSION
)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "renderer source-stamp test requires ${_required}")
    endif()
endforeach()
if(GIT_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "renderer source-stamp generator test requires Git")
endif()

set(_STAMP_TEST_COMPILER_EXECUTABLE "${TEST_COMPILER_EXECUTABLE}")
set(_STAMP_TEST_COMPILER_ARG1 "")
set(_STAMP_TEST_COMPILER_ID "${TEST_COMPILER_ID}")
set(_STAMP_TEST_COMPILER_VERSION "${TEST_COMPILER_VERSION}")
set(_STAMP_TEST_CONFIGURED_TARGET "")
set(_STAMP_TEST_CXX_FLAGS "")
set(_STAMP_TEST_CXX_CONFIG_FLAGS "-O3 -DNDEBUG")
set(_STAMP_TEST_IS_MULTI_CONFIG FALSE)
set(_STAMP_TEST_TOOLCHAIN_QUERY_PERMITTED TRUE)

function(_stamp_test_run)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
    )
    if(NOT _result EQUAL 0)
        string(JOIN " " _command ${ARGN})
        message(FATAL_ERROR
                "command failed (${_command}):\n${_output}\n${_error}")
    endif()
endfunction()

function(_stamp_test_generate _source_root _output_header _git_executable
         _state_output _head_output _digest_output)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE_ROOT=${_source_root}"
            "-DOUTPUT_HEADER=${_output_header}"
            "-DGIT_EXECUTABLE=${_git_executable}"
            "-DCOMPILER_EXECUTABLE=${_STAMP_TEST_COMPILER_EXECUTABLE}"
            "-DCOMPILER_ARG1=${_STAMP_TEST_COMPILER_ARG1}"
            "-DCOMPILER_ID=${_STAMP_TEST_COMPILER_ID}"
            "-DCOMPILER_VERSION=${_STAMP_TEST_COMPILER_VERSION}"
            "-DCONFIGURED_TARGET=${_STAMP_TEST_CONFIGURED_TARGET}"
            "-DCXX_FLAGS=${_STAMP_TEST_CXX_FLAGS}"
            "-DCXX_CONFIG_FLAGS=${_STAMP_TEST_CXX_CONFIG_FLAGS}"
            "-DIS_MULTI_CONFIG=${_STAMP_TEST_IS_MULTI_CONFIG}"
            "-DTOOLCHAIN_QUERY_PERMITTED=${_STAMP_TEST_TOOLCHAIN_QUERY_PERMITTED}"
            -DSYSTEM_NAME=Linux
            -P "${GENERATOR_SCRIPT}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "stamp generation failed:\n${_output}\n${_error}")
    endif()
    file(READ "${_output_header}" _header)
    string(REGEX MATCH
           "kRendererSourceState = \"([^\"]*)\"" _state_match "${_header}")
    set(_state "${CMAKE_MATCH_1}")
    string(REGEX MATCH
           "kRendererFullGitHead = \"([^\"]*)\"" _head_match "${_header}")
    set(_head "${CMAKE_MATCH_1}")
    string(REGEX MATCH
           "kRendererSourceClosureSha256 = \"([^\"]*)\"" _digest_match
           "${_header}")
    set(_digest "${CMAKE_MATCH_1}")
    string(REGEX MATCH
           "kRendererTargetTriple = \"([^\"]*)\"" _target_match "${_header}")
    set(_target "${CMAKE_MATCH_1}")
    set(${_state_output} "${_state}" PARENT_SCOPE)
    set(${_head_output} "${_head}" PARENT_SCOPE)
    set(${_digest_output} "${_digest}" PARENT_SCOPE)
    set(_stamp_test_last_target "${_target}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(_repository "${TEST_ROOT}/repository")
set(_outputs "${TEST_ROOT}/outputs")
file(MAKE_DIRECTORY
     "${_repository}/cmake" "${_repository}/include" "${_repository}/src"
     "${_outputs}")
file(WRITE "${_repository}/CMakeLists.txt"
     "cmake_minimum_required(VERSION 3.21)\nproject(stamp-fixture LANGUAGES CXX)\n")
file(WRITE "${_repository}/cmake/helper.cmake" "set(stamp_fixture TRUE)\n")
file(WRITE "${_repository}/include/value.hpp" "#pragma once\n#define VALUE 1\n")
file(WRITE "${_repository}/src/value.cpp" "int value() { return 1; }\n")
file(WRITE "${_repository}/.gitignore" "*.ignored\n")
file(WRITE "${_repository}/src/nonidentity.ignored" "ignored\n")

_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" init -q)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" config user.name
                renderer-stamp-test)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" config user.email
                renderer-stamp-test@example.invalid)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" add CMakeLists.txt
                .gitignore cmake include src/value.cpp)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" add --force
                src/nonidentity.ignored)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" commit -q -m initial)

_stamp_test_generate(
    "${_repository}" "${_outputs}/clean.hpp" "${GIT_EXECUTABLE}"
    _clean_state _clean_head _clean_digest
)
set(_clean_target "${_stamp_test_last_target}")
if(NOT _clean_state STREQUAL "clean")
    message(FATAL_ERROR "clean repository generated state '${_clean_state}'")
endif()
string(LENGTH "${_clean_head}" _clean_head_length)
if(NOT (_clean_head_length EQUAL 40 OR _clean_head_length EQUAL 64) OR
   NOT _clean_head MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "clean repository did not embed its full Git HEAD")
endif()
string(LENGTH "${_clean_digest}" _clean_digest_length)
if(NOT _clean_digest_length EQUAL 64 OR
   NOT _clean_digest MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "clean repository did not embed a SHA-256 closure digest")
endif()
if(NOT _clean_target MATCHES
       "^[a-z0-9_][a-z0-9._+]*(-[a-z0-9_][a-z0-9._+]*)+$")
    message(FATAL_ERROR "clean repository did not embed a valid target triple")
endif()

# Response files can change without invalidating already-built objects. They are
# therefore inadmissible even when a compiler could report their current target.
set(_response_file "${TEST_ROOT}/target-options.rsp")
if(TEST_COMPILER_ID STREQUAL "Clang")
    file(WRITE "${_response_file}" "--target=x86_64-unknown-linux-gnu\n")
else()
    file(WRITE "${_response_file}" "-m64\n")
endif()
set(_STAMP_TEST_CXX_FLAGS "@${_response_file}")
_stamp_test_generate(
    "${_repository}" "${_outputs}/response-target.hpp" "${GIT_EXECUTABLE}"
    _response_first_state _unused_head _unused_digest
)
if(NOT _response_first_state STREQUAL "unavailable" OR
   NOT _stamp_test_last_target STREQUAL "")
    message(FATAL_ERROR "response-file flags produced admissible evidence")
endif()

if(TEST_COMPILER_ID STREQUAL "Clang")
    file(WRITE "${_response_file}" "--target=i386-unknown-linux-gnu\n")
else()
    file(WRITE "${_response_file}" "-m32\n")
endif()
_stamp_test_generate(
    "${_repository}" "${_outputs}/response-target.hpp" "${GIT_EXECUTABLE}"
    _response_second_state _unused_head _unused_digest
)
if(NOT _response_second_state STREQUAL "unavailable" OR
   NOT _stamp_test_last_target STREQUAL "")
    message(FATAL_ERROR
            "edited response-file flags escaped fail-closed admission")
endif()
set(_STAMP_TEST_CXX_FLAGS "")

# Multiple configurations have no single effective renderer command. Reject all
# of them rather than selecting one configuration or fabricating a common target.
set(_STAMP_TEST_IS_MULTI_CONFIG TRUE)
_stamp_test_generate(
    "${_repository}" "${_outputs}/multi-config.hpp" "${GIT_EXECUTABLE}"
    _multi_config_state _unused_head _unused_digest
)
if(NOT _multi_config_state STREQUAL "unavailable" OR
   NOT _stamp_test_last_target STREQUAL "")
    message(FATAL_ERROR "multi-config build produced admissible evidence")
endif()
set(_STAMP_TEST_IS_MULTI_CONFIG FALSE)

set(_STAMP_TEST_CONFIGURED_TARGET "${_clean_target}")
_stamp_test_generate(
    "${_repository}" "${_outputs}/configured-target.hpp" "${GIT_EXECUTABLE}"
    _configured_target_state _unused_head _unused_digest
)
if(NOT _configured_target_state STREQUAL "unavailable" OR
   NOT _stamp_test_last_target STREQUAL "")
    message(FATAL_ERROR "caller-configured target produced admissible evidence")
endif()
set(_STAMP_TEST_CONFIGURED_TARGET "")

set(_STAMP_TEST_CXX_CONFIG_FLAGS "-O2 -DNDEBUG")
_stamp_test_generate(
    "${_repository}" "${_outputs}/noncanonical-release.hpp"
    "${GIT_EXECUTABLE}" _noncanonical_release_state _unused_head _unused_digest
)
if(NOT _noncanonical_release_state STREQUAL "unavailable" OR
   NOT _stamp_test_last_target STREQUAL "")
    message(FATAL_ERROR "noncanonical Release flags produced admissible evidence")
endif()
set(_STAMP_TEST_CXX_CONFIG_FLAGS "-O3 -DNDEBUG")

file(APPEND "${_repository}/src/value.cpp" "// tracked dirty\n")
_stamp_test_generate(
    "${_repository}" "${_outputs}/tracked-dirty.hpp" "${GIT_EXECUTABLE}"
    _tracked_state _unused_head _unused_digest
)
if(NOT _tracked_state STREQUAL "dirty")
    message(FATAL_ERROR "tracked modification was not classified dirty")
endif()
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" checkout -q --
                src/value.cpp)

_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" update-index
                --assume-unchanged src/value.cpp)
file(APPEND "${_repository}/src/value.cpp" "// hidden tracked change\n")
_stamp_test_generate(
    "${_repository}" "${_outputs}/assume-unchanged.hpp" "${GIT_EXECUTABLE}"
    _assume_unchanged_state _unused_head _unused_digest
)
if(NOT _assume_unchanged_state STREQUAL "unavailable")
    message(FATAL_ERROR "assume-unchanged index state was admissible")
endif()
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" update-index
                --no-assume-unchanged src/value.cpp)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" checkout -q --
                src/value.cpp)

_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" update-index
                --skip-worktree src/value.cpp)
_stamp_test_generate(
    "${_repository}" "${_outputs}/skip-worktree.hpp" "${GIT_EXECUTABLE}"
    _skip_worktree_state _unused_head _unused_digest
)
if(NOT _skip_worktree_state STREQUAL "unavailable")
    message(FATAL_ERROR "skip-worktree index state was admissible")
endif()
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" update-index
                --no-skip-worktree src/value.cpp)

file(APPEND "${_repository}/include/value.hpp" "// staged dirty\n")
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" add include/value.hpp)
_stamp_test_generate(
    "${_repository}" "${_outputs}/staged-dirty.hpp" "${GIT_EXECUTABLE}"
    _staged_state _unused_head _unused_digest
)
if(NOT _staged_state STREQUAL "dirty")
    message(FATAL_ERROR "staged modification was not classified dirty")
endif()
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" reset -q --hard HEAD)

file(WRITE "${_repository}/src/untracked.cpp" "int untracked() { return 2; }\n")
_stamp_test_generate(
    "${_repository}" "${_outputs}/untracked-dirty.hpp" "${GIT_EXECUTABLE}"
    _untracked_state _unused_head _unused_digest
)
if(NOT _untracked_state STREQUAL "dirty")
    message(FATAL_ERROR "untracked closure file was not classified dirty")
endif()
file(REMOVE "${_repository}/src/untracked.cpp")

file(WRITE "${_repository}/src/value.cpp" "int value() { return 3; }\n")
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" add src/value.cpp)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" commit -q -m changed)
_stamp_test_generate(
    "${_repository}" "${_outputs}/changed.hpp" "${GIT_EXECUTABLE}"
    _changed_state _changed_head _changed_digest
)
if(NOT _changed_state STREQUAL "clean")
    message(FATAL_ERROR "changed committed repository was not clean")
endif()
if(_changed_head STREQUAL _clean_head)
    message(FATAL_ERROR "new commit did not change the embedded Git HEAD")
endif()
if(_changed_digest STREQUAL _clean_digest)
    message(FATAL_ERROR "source-content change did not change the closure digest")
endif()

file(REMOVE "${_repository}/src/value.cpp")
_stamp_test_generate(
    "${_repository}" "${_outputs}/tracked-deletion.hpp" "${GIT_EXECUTABLE}"
    _deletion_state _unused_head _unused_digest
)
if(NOT _deletion_state STREQUAL "dirty")
    message(FATAL_ERROR "tracked deletion was not classified dirty")
endif()
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" checkout -q --
                src/value.cpp)

file(WRITE "${_repository}/src/masked.cpp" "int masked() { return 5; }\n")
file(APPEND "${_repository}/.gitignore" "src/masked.cpp\n")
_stamp_test_generate(
    "${_repository}" "${_outputs}/dirty-ignore-rule.hpp" "${GIT_EXECUTABLE}"
    _dirty_ignore_state _unused_head _unused_digest
)
if(NOT _dirty_ignore_state STREQUAL "dirty")
    message(FATAL_ERROR "dirty ignore rule hid an untracked closure file")
endif()
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" checkout -q -- .gitignore)
file(REMOVE "${_repository}/src/masked.cpp")

file(WRITE "${_repository}/src/locally-hidden.cpp"
     "int locally_hidden() { return 6; }\n")
file(APPEND "${_repository}/.git/info/exclude" "src/locally-hidden.cpp\n")
_stamp_test_generate(
    "${_repository}" "${_outputs}/local-exclude.hpp" "${GIT_EXECUTABLE}"
    _local_exclude_state _unused_head _unused_digest
)
if(NOT _local_exclude_state STREQUAL "dirty")
    message(FATAL_ERROR "local Git exclude hid an untracked closure file")
endif()
file(REMOVE "${_repository}/src/locally-hidden.cpp")

set(_semicolon_path "${_repository}/src/unsupported;path.cpp")
file(WRITE "${_semicolon_path}" "int unsupported_path() { return 4; }\n")
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" add --all)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" commit -q -m
                unsupported-path)
_stamp_test_generate(
    "${_repository}" "${_outputs}/unsupported-path.hpp" "${GIT_EXECUTABLE}"
    _unsupported_path_state _unused_head _unused_digest
)
if(NOT _unsupported_path_state STREQUAL "unavailable")
    message(FATAL_ERROR
            "unrepresentable clean repository path was not unavailable")
endif()

file(REMOVE "${_semicolon_path}")
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" add --update)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" commit -q -m
                remove-unsupported-path)

file(CREATE_LINK "${_repository}/src/value.cpp"
                 "${_repository}/src/value-link.cpp" SYMBOLIC)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" add src/value-link.cpp)
_stamp_test_run("${GIT_EXECUTABLE}" -C "${_repository}" commit -q -m symlink)
_stamp_test_generate(
    "${_repository}" "${_outputs}/symlink.hpp" "${GIT_EXECUTABLE}"
    _symlink_state _unused_head _unused_digest
)
if(NOT _symlink_state STREQUAL "unavailable")
    message(FATAL_ERROR "clean symlink closure was not unavailable")
endif()

_stamp_test_generate(
    "${_repository}/src" "${_outputs}/nested-root.hpp" "${GIT_EXECUTABLE}"
    _nested_state _unused_head _unused_digest
)
if(NOT _nested_state STREQUAL "unavailable")
    message(FATAL_ERROR "nested source root inherited its parent repository stamp")
endif()

set(_plain_tree "${TEST_ROOT}/plain-tree")
file(MAKE_DIRECTORY "${_plain_tree}/src")
file(WRITE "${_plain_tree}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.21)\n")
file(WRITE "${_plain_tree}/src/value.cpp" "int value() { return 1; }\n")
_stamp_test_generate(
    "${_plain_tree}" "${_outputs}/unavailable.hpp" ""
    _unavailable_state _unavailable_head _unavailable_digest
)
if(NOT _unavailable_state STREQUAL "unavailable" OR
   NOT _unavailable_head STREQUAL "" OR
   NOT _unavailable_digest STREQUAL "")
    message(FATAL_ERROR "unavailable Git fabricated admissible source evidence")
endif()
