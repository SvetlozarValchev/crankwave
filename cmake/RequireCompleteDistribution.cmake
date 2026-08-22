cmake_minimum_required(VERSION 3.21)

foreach(_required IN ITEMS
        SOURCE_STAMP
        SOURCE_ROOT
        RELEASE_SOURCE_INPUTS
        RELEASE_SOURCE_IDENTITY_SCRIPT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR
            "crankwave_distribution requires ${_required}")
    endif()
endforeach()

if(NOT EXISTS "${SOURCE_STAMP}" OR IS_DIRECTORY "${SOURCE_STAMP}" OR
   IS_SYMLINK "${SOURCE_STAMP}")
    message(FATAL_ERROR
        "crankwave_distribution source stamp is absent: ${SOURCE_STAMP}")
endif()
file(READ "${SOURCE_STAMP}" _stamp)
if(NOT _stamp MATCHES "kRendererSourceState = \"clean\";")
    message(FATAL_ERROR
        "crankwave_distribution requires a clean renderer source closure")
endif()
if(NOT _stamp MATCHES "kRendererToolchainState = \"available\";")
    message(FATAL_ERROR
        "crankwave_distribution requires the canonical release toolchain")
endif()
string(REGEX MATCH
    "kRendererFullGitHead = \"([0-9a-f]+)\";"
    _git_match "${_stamp}")
set(_git_commit "${CMAKE_MATCH_1}")
string(LENGTH "${_git_commit}" _git_commit_length)
string(REGEX MATCH
    "kRendererSourceClosureSha256 = \"([0-9a-f]+)\";"
    _closure_match "${_stamp}")
set(_source_closure "${CMAKE_MATCH_1}")
string(LENGTH "${_source_closure}" _source_closure_length)
if(NOT _git_match OR
   NOT (_git_commit_length EQUAL 40 OR _git_commit_length EQUAL 64) OR
   NOT _closure_match OR NOT _source_closure_length EQUAL 64)
    message(FATAL_ERROR
        "crankwave_distribution requires complete source identity")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        "-DSOURCE_ROOT=${SOURCE_ROOT}"
        "-DINPUT_PATHS_FILE=${RELEASE_SOURCE_INPUTS}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        -P "${RELEASE_SOURCE_IDENTITY_SCRIPT}"
    RESULT_VARIABLE _release_source_result
    OUTPUT_VARIABLE _release_source_json
    ERROR_VARIABLE _release_source_stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _release_source_result EQUAL 0 OR
   NOT _release_source_stderr STREQUAL "")
    message(FATAL_ERROR
        "crankwave_distribution release source identity failed")
endif()
string(JSON _release_source_state GET "${_release_source_json}" state)
string(JSON _release_source_head GET
    "${_release_source_json}" git_commit)
string(JSON _release_source_closure GET
    "${_release_source_json}" closure_sha256)
string(LENGTH "${_release_source_closure}" _release_source_closure_length)
if(NOT _release_source_state STREQUAL "clean" OR
   NOT _release_source_head STREQUAL _git_commit OR
   NOT _release_source_closure MATCHES "^[0-9a-f]+$" OR
   NOT _release_source_closure_length EQUAL 64)
    message(FATAL_ERROR
        "crankwave_distribution requires clean installed release inputs")
endif()
