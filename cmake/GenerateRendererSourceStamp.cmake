cmake_minimum_required(VERSION 3.21)

# Build-time generator for the private renderer source/toolchain stamp.
#
# The source-closure digest is SHA-256 over this canonical UTF-8 byte stream:
#
#   engine-sim-offline.renderer-source-closure.v1\n
#   <normalized-relative-path>\n
#   <decimal-byte-count>\n
#   <lowercase-file-sha256>\n
#   ...
#
# Records are sorted by normalized relative path. The closure contains every tracked
# and untracked regular file selected by the CMakeLists.txt, cmake/, include/, and src/
# pathspecs; Git ignore configuration cannot hide a renderer input. Repository paths
# outside a conservative portable spelling are rejected as unavailable instead of
# being serialized ambiguously.
#
# Toolchain identity is admitted only for the canonical top-level, single-config
# Release command selected by RendererSourceStamp.cmake. Target discovery remains in
# this always-run script so an edited compiler wrapper cannot leave a configured-time
# target guess embedded in a newly generated stamp.

foreach(
    _required
    IN ITEMS
        SOURCE_ROOT
        OUTPUT_HEADER
        COMPILER_EXECUTABLE
        COMPILER_ARG1
        COMPILER_ID
        COMPILER_VERSION
        CONFIGURED_TARGET
        CXX_FLAGS
        CXX_CONFIG_FLAGS
        IS_MULTI_CONFIG
        TOOLCHAIN_QUERY_PERMITTED
        SYSTEM_NAME
)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "GenerateRendererSourceStamp.cmake requires ${_required}")
    endif()
endforeach()

cmake_path(ABSOLUTE_PATH SOURCE_ROOT NORMALIZE OUTPUT_VARIABLE _source_root)
cmake_path(ABSOLUTE_PATH OUTPUT_HEADER NORMALIZE OUTPUT_VARIABLE _output_header)

function(_renderer_stamp_escape_cpp _output _input)
    set(_value "${_input}")
    string(REPLACE "\\" "\\\\" _value "${_value}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    string(REPLACE "\r" "\\r" _value "${_value}")
    string(REPLACE "\n" "\\n" _value "${_value}")
    string(REPLACE "\t" "\\t" _value "${_value}")
    set(${_output} "${_value}" PARENT_SCOPE)
endfunction()

function(_renderer_stamp_write_header _state _head _closure_sha256)
    _renderer_stamp_escape_cpp(_state_cpp "${_state}")
    _renderer_stamp_escape_cpp(_head_cpp "${_head}")
    _renderer_stamp_escape_cpp(_closure_cpp "${_closure_sha256}")
    _renderer_stamp_escape_cpp(_compiler_id_cpp "${COMPILER_ID}")
    _renderer_stamp_escape_cpp(_compiler_version_cpp "${COMPILER_VERSION}")
    _renderer_stamp_escape_cpp(_target_triple_cpp "${_target_triple}")

    get_filename_component(_output_directory "${_output_header}" DIRECTORY)
    file(MAKE_DIRECTORY "${_output_directory}")
    set(_temporary "${_output_header}.tmp")
    file(WRITE "${_temporary}"
        "#pragma once\n\n"
        "#include <string_view>\n\n"
        "namespace engine_sim_offline::determinism::generated {\n\n"
        "inline constexpr std::string_view kRendererSourceState = \"${_state_cpp}\";\n"
        "inline constexpr std::string_view kRendererFullGitHead = \"${_head_cpp}\";\n"
        "inline constexpr std::string_view kRendererSourceClosureSha256 = \"${_closure_cpp}\";\n"
        "inline constexpr std::string_view kRendererCompilerId = \"${_compiler_id_cpp}\";\n"
        "inline constexpr std::string_view kRendererCompilerVersion = \"${_compiler_version_cpp}\";\n"
        "inline constexpr std::string_view kRendererTargetTriple = \"${_target_triple_cpp}\";\n\n"
        "} // namespace engine_sim_offline::determinism::generated\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_temporary}"
                "${_output_header}"
        RESULT_VARIABLE _copy_result
    )
    file(REMOVE "${_temporary}")
    if(NOT _copy_result EQUAL 0)
        message(FATAL_ERROR "could not publish generated renderer source stamp header")
    endif()
endfunction()

set(_target_triple "")
set(_toolchain_supported TRUE)
if(NOT SYSTEM_NAME STREQUAL "Linux" OR
   NOT COMPILER_ID MATCHES "^(GNU|Clang)$" OR
   COMPILER_VERSION STREQUAL "" OR COMPILER_EXECUTABLE STREQUAL "" OR
   IS_MULTI_CONFIG OR NOT TOOLCHAIN_QUERY_PERMITTED OR
   NOT COMPILER_ARG1 STREQUAL "" OR
   NOT CONFIGURED_TARGET STREQUAL "" OR
   NOT CXX_FLAGS STREQUAL "" OR
   NOT CXX_CONFIG_FLAGS STREQUAL "-O3 -DNDEBUG" OR
   COMPILER_EXECUTABLE MATCHES "[;\r\n]" OR
   COMPILER_ARG1 MATCHES "[;\r\n]" OR
   CONFIGURED_TARGET MATCHES "[;\r\n]" OR
   CXX_FLAGS MATCHES "[;\r\n]" OR
   CXX_CONFIG_FLAGS MATCHES "[;\r\n]")
    set(_toolchain_supported FALSE)
endif()

if(_toolchain_supported)
    set(_compiler_command "${COMPILER_EXECUTABLE}")
    separate_arguments(_configuration_flags UNIX_COMMAND
                       "${CXX_CONFIG_FLAGS}")
    list(APPEND _compiler_command ${_configuration_flags})

    if(COMPILER_ID STREQUAL "GNU")
        list(APPEND _compiler_command -print-multiarch)
    else()
        list(APPEND _compiler_command -dumpmachine)
    endif()
    execute_process(
        COMMAND ${_compiler_command}
        RESULT_VARIABLE _target_result
        OUTPUT_VARIABLE _target_triple
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT _target_result EQUAL 0 OR
       NOT _target_triple MATCHES
           "^[a-z0-9_][a-z0-9._+]*(-[a-z0-9_][a-z0-9._+]*)+$")
        set(_target_triple "")
        set(_toolchain_supported FALSE)
    endif()
endif()

if(NOT DEFINED GIT_EXECUTABLE OR GIT_EXECUTABLE STREQUAL "" OR
   NOT _toolchain_supported)
    _renderer_stamp_write_header("unavailable" "" "")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_source_root}" rev-parse
            --is-inside-work-tree
    RESULT_VARIABLE _inside_result
    OUTPUT_VARIABLE _inside_work_tree
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _inside_result EQUAL 0 OR NOT _inside_work_tree STREQUAL "true")
    _renderer_stamp_write_header("unavailable" "" "")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_source_root}" rev-parse --show-toplevel
    RESULT_VARIABLE _top_level_result
    OUTPUT_VARIABLE _git_top_level
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(_top_level_result EQUAL 0)
    cmake_path(NORMAL_PATH _git_top_level OUTPUT_VARIABLE _git_top_level)
endif()
if(NOT _top_level_result EQUAL 0 OR NOT _git_top_level STREQUAL _source_root)
    _renderer_stamp_write_header("unavailable" "" "")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_source_root}" rev-parse --verify HEAD
    RESULT_VARIABLE _head_result
    OUTPUT_VARIABLE _full_head
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(LENGTH "${_full_head}" _head_length)
if(NOT _head_result EQUAL 0 OR
   NOT (_head_length EQUAL 40 OR _head_length EQUAL 64) OR
   NOT _full_head MATCHES "^[0-9a-f]+$" OR
   NOT _full_head MATCHES "[1-9a-f]")
    _renderer_stamp_write_header("unavailable" "" "")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -c core.quotePath=false -C "${_source_root}"
            ls-files --cached --others -- CMakeLists.txt cmake include src
    RESULT_VARIABLE _files_result
    OUTPUT_VARIABLE _files_output
    ERROR_QUIET
)
if(NOT _files_result EQUAL 0)
    _renderer_stamp_write_header("unavailable" "${_full_head}" "")
    return()
endif()

string(REPLACE "\r\n" "\n" _files_output "${_files_output}")
if(_files_output MATCHES ";" OR _files_output MATCHES "\r")
    _renderer_stamp_write_header("unavailable" "${_full_head}" "")
    return()
endif()
string(REGEX REPLACE "\n$" "" _files_output "${_files_output}")
if(_files_output STREQUAL "")
    _renderer_stamp_write_header("unavailable" "${_full_head}" "")
    return()
endif()
string(REPLACE "\n" ";" _closure_paths "${_files_output}")
list(REMOVE_DUPLICATES _closure_paths)
list(SORT _closure_paths)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -c core.quotePath=false -C "${_source_root}"
            ls-files -v -- CMakeLists.txt cmake include src
    RESULT_VARIABLE _index_result
    OUTPUT_VARIABLE _index_output
    ERROR_QUIET
)
string(REPLACE "\r\n" "\n" _index_output "${_index_output}")
if(_index_output MATCHES ";" OR _index_output MATCHES "\r")
    set(_index_result 1)
endif()
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

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -c core.quotePath=false -C "${_source_root}"
            ls-files --others -- CMakeLists.txt cmake include src
    RESULT_VARIABLE _untracked_result
    OUTPUT_VARIABLE _untracked_output
    ERROR_QUIET
)
string(REPLACE "\r\n" "\n" _untracked_output "${_untracked_output}")
if(_untracked_output MATCHES ";" OR _untracked_output MATCHES "\r")
    set(_untracked_result 1)
endif()

set(_canonical "engine-sim-offline.renderer-source-closure.v1\n")
set(_closure_complete TRUE)
foreach(_relative_path IN LISTS _closure_paths)
    if(NOT _relative_path MATCHES "^[A-Za-z0-9._/+@-]+$" OR
       _relative_path MATCHES "(^|/)\.\.(/|$)" OR
       IS_DIRECTORY "${_source_root}/${_relative_path}" OR
       IS_SYMLINK "${_source_root}/${_relative_path}" OR
       NOT EXISTS "${_source_root}/${_relative_path}")
        set(_closure_complete FALSE)
        break()
    endif()
    file(SIZE "${_source_root}/${_relative_path}" _file_size)
    file(SHA256 "${_source_root}/${_relative_path}" _file_sha256)
    string(APPEND _canonical
           "${_relative_path}\n${_file_size}\n${_file_sha256}\n")
endforeach()

if(_closure_complete)
    string(SHA256 _closure_sha256 "${_canonical}")
else()
    set(_closure_sha256 "")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_source_root}" status --porcelain=v1
            --untracked-files=no -- CMakeLists.txt cmake include src
    RESULT_VARIABLE _status_result
    OUTPUT_VARIABLE _status_output
    ERROR_QUIET
)
if(NOT _status_result EQUAL 0 OR NOT _index_result EQUAL 0 OR
   NOT _untracked_result EQUAL 0 OR NOT _ordinary_index)
    _renderer_stamp_write_header("unavailable" "${_full_head}"
                                 "${_closure_sha256}")
elseif(NOT _status_output STREQUAL "" OR NOT _untracked_output STREQUAL "")
    _renderer_stamp_write_header("dirty" "${_full_head}" "${_closure_sha256}")
elseif(NOT _closure_complete)
    _renderer_stamp_write_header("unavailable" "${_full_head}"
                                 "${_closure_sha256}")
else()
    _renderer_stamp_write_header("clean" "${_full_head}" "${_closure_sha256}")
endif()
