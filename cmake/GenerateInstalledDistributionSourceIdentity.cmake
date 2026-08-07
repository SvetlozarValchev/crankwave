cmake_minimum_required(VERSION 3.21)

foreach(_required IN ITEMS SOURCE_ROOT INPUT_PATHS_FILE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR
            "installed distribution source identity requires ${_required}")
    endif()
endforeach()

cmake_path(ABSOLUTE_PATH SOURCE_ROOT NORMALIZE OUTPUT_VARIABLE _source_root)
if(NOT EXISTS "${INPUT_PATHS_FILE}" OR IS_DIRECTORY "${INPUT_PATHS_FILE}" OR
   IS_SYMLINK "${INPUT_PATHS_FILE}")
    message(FATAL_ERROR
        "installed distribution source input list is unavailable")
endif()
file(READ "${INPUT_PATHS_FILE}" _input_text)
string(REPLACE "\r\n" "\n" _input_text "${_input_text}")
if(_input_text MATCHES "[;\r]")
    message(FATAL_ERROR
        "installed distribution source input list is not canonical")
endif()
string(REGEX REPLACE "\n$" "" _input_text "${_input_text}")
if(_input_text STREQUAL "")
    message(FATAL_ERROR
        "installed distribution source input list is empty")
endif()
string(REPLACE "\n" ";" _input_paths "${_input_text}")
set(_prior_input "")
set(_canonical
    "engine-sim-offline.installed-distribution-source-closure.v1\n")
foreach(_relative_path IN LISTS _input_paths)
    if(NOT _relative_path MATCHES "^[A-Za-z0-9._+/@-]+$" OR
       _relative_path MATCHES "(^|/)\.\.(/|$)" OR
       (NOT _prior_input STREQUAL "" AND
        NOT _prior_input STRLESS _relative_path))
        message(FATAL_ERROR
            "installed distribution source path is not canonical: ${_relative_path}")
    endif()
    set(_prior_input "${_relative_path}")
    set(_absolute_path "${_source_root}/${_relative_path}")
    if(NOT EXISTS "${_absolute_path}" OR IS_DIRECTORY "${_absolute_path}" OR
       IS_SYMLINK "${_absolute_path}")
        message(FATAL_ERROR
            "installed distribution source input is not regular: ${_relative_path}")
    endif()
    file(SIZE "${_absolute_path}" _bytes)
    file(SHA256 "${_absolute_path}" _sha256)
    string(APPEND _canonical
        "${_relative_path}\n${_bytes}\n${_sha256}\n")
endforeach()
string(SHA256 _closure_sha256 "${_canonical}")

set(_state unavailable)
set(_git_commit "")
if(DEFINED GIT_EXECUTABLE AND NOT "${GIT_EXECUTABLE}" STREQUAL "" AND
   EXISTS "${GIT_EXECUTABLE}" AND NOT IS_DIRECTORY "${GIT_EXECUTABLE}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${_source_root}"
            rev-parse --show-toplevel
        RESULT_VARIABLE _top_level_result
        OUTPUT_VARIABLE _top_level
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_top_level_result EQUAL 0)
        cmake_path(NORMAL_PATH _top_level OUTPUT_VARIABLE _top_level)
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${_source_root}"
            rev-parse --verify HEAD
        RESULT_VARIABLE _head_result
        OUTPUT_VARIABLE _git_commit
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(LENGTH "${_git_commit}" _head_length)
    if(_top_level_result EQUAL 0 AND _top_level STREQUAL _source_root AND
       _head_result EQUAL 0 AND
       (_head_length EQUAL 40 OR _head_length EQUAL 64) AND
       _git_commit MATCHES "^[0-9a-f]+$")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -c core.quotePath=false
                -C "${_source_root}" status --porcelain=v1
                --untracked-files=all -- ${_input_paths}
            RESULT_VARIABLE _status_result
            OUTPUT_VARIABLE _status_output
            ERROR_QUIET
        )
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -c core.quotePath=false
                -C "${_source_root}" ls-files --cached -- ${_input_paths}
            RESULT_VARIABLE _tracked_result
            OUTPUT_VARIABLE _tracked_output
            ERROR_QUIET
        )
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -c core.quotePath=false
                -C "${_source_root}" ls-files -v -- ${_input_paths}
            RESULT_VARIABLE _index_result
            OUTPUT_VARIABLE _index_output
            ERROR_QUIET
        )
        string(REPLACE "\r\n" "\n" _status_output "${_status_output}")
        string(REPLACE "\r\n" "\n" _tracked_output "${_tracked_output}")
        string(REPLACE "\r\n" "\n" _index_output "${_index_output}")
        string(REGEX REPLACE "\n$" "" _tracked_output "${_tracked_output}")
        if(_tracked_output STREQUAL "")
            set(_tracked_paths)
        else()
            string(REPLACE "\n" ";" _tracked_paths "${_tracked_output}")
        endif()
        list(REMOVE_DUPLICATES _tracked_paths)
        list(SORT _tracked_paths)
        string(REGEX REPLACE "\n$" "" _index_output "${_index_output}")
        set(_ordinary_index TRUE)
        if(NOT _index_output STREQUAL "")
            string(REPLACE "\n" ";" _index_records "${_index_output}")
            foreach(_index_record IN LISTS _index_records)
                if(NOT _index_record MATCHES "^H ")
                    set(_ordinary_index FALSE)
                    break()
                endif()
            endforeach()
        endif()
        if(_status_result EQUAL 0 AND _tracked_result EQUAL 0 AND
           _index_result EQUAL 0 AND _ordinary_index AND
           _status_output STREQUAL "" AND
           "${_tracked_paths}" STREQUAL "${_input_paths}")
            set(_state clean)
        elseif(_status_result EQUAL 0 AND _tracked_result EQUAL 0 AND
               _index_result EQUAL 0)
            set(_state dirty)
        endif()
    endif()
endif()

if(_git_commit MATCHES "^[0-9a-f]+$")
    set(_git_commit_json "\"${_git_commit}\"")
else()
    set(_git_commit_json null)
endif()
set(_identity
    "{\"state\":\"${_state}\",\"git_commit\":${_git_commit_json},\"closure_sha256\":\"${_closure_sha256}\"}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${_identity}")
