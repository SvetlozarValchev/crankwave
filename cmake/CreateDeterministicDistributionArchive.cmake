cmake_minimum_required(VERSION 3.21)

foreach(_required IN ITEMS
        PREFIX_DIRECTORY
        RESOURCE_RELATIVE_DIRECTORY
        RELEASE_IDENTITY
        OUTPUT_ARCHIVE
        TAR_EXECUTABLE
        REQUIRE_COMPLETE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR
            "deterministic distribution archive requires ${_required}")
    endif()
endforeach()

if(IS_ABSOLUTE "${RESOURCE_RELATIVE_DIRECTORY}" OR
   NOT RESOURCE_RELATIVE_DIRECTORY MATCHES "^[A-Za-z0-9._+/@-]+$" OR
   RESOURCE_RELATIVE_DIRECTORY MATCHES "(^|/)\.\.(/|$)")
    message(FATAL_ERROR
        "distribution resource directory is not portable: ${RESOURCE_RELATIVE_DIRECTORY}")
endif()

if(NOT EXISTS "${PREFIX_DIRECTORY}" OR
   NOT IS_DIRECTORY "${PREFIX_DIRECTORY}" OR
   IS_SYMLINK "${PREFIX_DIRECTORY}")
    message(FATAL_ERROR
        "distribution prefix is not a regular directory: ${PREFIX_DIRECTORY}")
endif()
if(NOT EXISTS "${TAR_EXECUTABLE}" OR IS_DIRECTORY "${TAR_EXECUTABLE}")
    message(FATAL_ERROR "GNU tar executable is absent: ${TAR_EXECUTABLE}")
endif()
execute_process(
    COMMAND "${TAR_EXECUTABLE}" --version
    RESULT_VARIABLE _tar_version_result
    OUTPUT_VARIABLE _tar_version_stdout
    ERROR_VARIABLE _tar_version_stderr
)
if(NOT _tar_version_result EQUAL 0 OR
   NOT _tar_version_stdout MATCHES "^tar \\(GNU tar\\)")
    message(FATAL_ERROR
        "deterministic distribution archiving requires GNU tar")
endif()

get_filename_component(_archive_parent "${OUTPUT_ARCHIVE}" DIRECTORY)
file(MAKE_DIRECTORY "${_archive_parent}")
set(_sidecar "${OUTPUT_ARCHIVE}.sha256")
set(_temporary_archive "${OUTPUT_ARCHIVE}.tmp")
set(_entry_list "${OUTPUT_ARCHIVE}.entries.tmp")
foreach(_prior_output IN ITEMS
        "${OUTPUT_ARCHIVE}"
        "${_sidecar}"
        "${_temporary_archive}"
        "${_entry_list}"
        "${_sidecar}.tmp")
    if(IS_DIRECTORY "${_prior_output}")
        message(FATAL_ERROR
            "distribution archive output is a directory: ${_prior_output}")
    endif()
    file(REMOVE "${_prior_output}")
endforeach()

set(_resource_root "${PREFIX_DIRECTORY}/${RESOURCE_RELATIVE_DIRECTORY}")
set(_manifest "${_resource_root}/release.json")
set(_binding "${_resource_root}/release.json.sha256")
foreach(_release_file IN ITEMS "${_manifest}" "${_binding}")
    if(NOT EXISTS "${_release_file}" OR IS_DIRECTORY "${_release_file}")
        message(FATAL_ERROR
            "distribution release metadata is absent: ${_release_file}")
    endif()
endforeach()
file(READ "${_manifest}" _manifest_json)
string(JSON _manifest_release GET "${_manifest_json}" release_identity)
string(JSON _manifest_complete GET "${_manifest_json}" complete)
string(JSON _manifest_resource_root GET
    "${_manifest_json}" layout resource_root)
if(NOT _manifest_release STREQUAL RELEASE_IDENTITY)
    message(FATAL_ERROR "distribution release identity differs")
endif()
if(NOT _manifest_resource_root STREQUAL RESOURCE_RELATIVE_DIRECTORY)
    message(FATAL_ERROR "distribution resource directory differs")
endif()
if(REQUIRE_COMPLETE AND NOT _manifest_complete)
    message(FATAL_ERROR
        "an incomplete development install cannot become a release archive")
endif()
file(READ "${_binding}" _expected_manifest_sha256)
file(SHA256 "${_manifest}" _actual_manifest_sha256)
if(NOT _expected_manifest_sha256 STREQUAL "${_actual_manifest_sha256}\n")
    message(FATAL_ERROR "distribution manifest binding differs")
endif()

get_filename_component(_prefix_parent "${PREFIX_DIRECTORY}" DIRECTORY)
get_filename_component(_prefix_name "${PREFIX_DIRECTORY}" NAME)
if(NOT _prefix_name MATCHES "^[A-Za-z0-9._+-]+$")
    message(FATAL_ERROR "distribution prefix name is not portable")
endif()
string(JSON _file_count LENGTH "${_manifest_json}" files)
if(_file_count LESS 1)
    message(FATAL_ERROR "distribution manifest file closure is empty")
endif()
math(EXPR _file_last "${_file_count} - 1")
set(_archive_entry_list "${_prefix_name}")
set(_prior_relative_path "")
foreach(_file_index RANGE 0 ${_file_last})
    string(JSON _relative_path GET
        "${_manifest_json}" files ${_file_index} path)
    string(JSON _expected_bytes GET
        "${_manifest_json}" files ${_file_index} bytes)
    string(JSON _expected_sha256 GET
        "${_manifest_json}" files ${_file_index} sha256)
    if(NOT _relative_path MATCHES "^[A-Za-z0-9._+/@-]+$" OR
       _relative_path MATCHES "(^|/)\.\.(/|$)" OR
       (NOT _prior_relative_path STREQUAL "" AND
        NOT _prior_relative_path STRLESS _relative_path))
        message(FATAL_ERROR
            "distribution manifest path is not canonical: ${_relative_path}")
    endif()
    set(_prior_relative_path "${_relative_path}")
    set(_member "${PREFIX_DIRECTORY}/${_relative_path}")
    if(NOT EXISTS "${_member}" OR IS_DIRECTORY "${_member}" OR
       IS_SYMLINK "${_member}")
        message(FATAL_ERROR
            "distribution member is absent or not regular: ${_relative_path}")
    endif()
    file(SIZE "${_member}" _actual_bytes)
    file(SHA256 "${_member}" _actual_sha256)
    if(NOT _actual_bytes EQUAL _expected_bytes OR
       NOT _actual_sha256 STREQUAL _expected_sha256)
        message(FATAL_ERROR
            "distribution member identity differs: ${_relative_path}")
    endif()
    list(APPEND _archive_entry_list
        "${_prefix_name}/${_relative_path}")
endforeach()
foreach(_metadata_relative IN ITEMS
        "${RESOURCE_RELATIVE_DIRECTORY}/release.json"
        "${RESOURCE_RELATIVE_DIRECTORY}/release.json.sha256")
    set(_metadata_member "${PREFIX_DIRECTORY}/${_metadata_relative}")
    if(NOT EXISTS "${_metadata_member}" OR
       IS_DIRECTORY "${_metadata_member}" OR
       IS_SYMLINK "${_metadata_member}")
        message(FATAL_ERROR
            "distribution release metadata is not regular: ${_metadata_relative}")
    endif()
    list(APPEND _archive_entry_list
        "${_prefix_name}/${_metadata_relative}")
endforeach()
list(REMOVE_DUPLICATES _archive_entry_list)
set(_archive_file_entries ${_archive_entry_list})
foreach(_archive_file_entry IN LISTS _archive_file_entries)
    get_filename_component(_archive_parent_entry
        "${_archive_file_entry}" DIRECTORY)
    while(NOT _archive_parent_entry STREQUAL "" AND
          NOT _archive_parent_entry STREQUAL ".")
        set(_archive_parent_path
            "${_prefix_parent}/${_archive_parent_entry}")
        if(NOT EXISTS "${_archive_parent_path}" OR
           NOT IS_DIRECTORY "${_archive_parent_path}" OR
           IS_SYMLINK "${_archive_parent_path}")
            message(FATAL_ERROR
                "distribution parent is absent or not regular: ${_archive_parent_entry}")
        endif()
        list(APPEND _archive_entry_list "${_archive_parent_entry}")
        if(_archive_parent_entry STREQUAL _prefix_name)
            break()
        endif()
        get_filename_component(_archive_parent_entry
            "${_archive_parent_entry}" DIRECTORY)
    endwhile()
endforeach()
list(REMOVE_DUPLICATES _archive_entry_list)
list(SORT _archive_entry_list)
string(REPLACE ";" "\n" _archive_entries "${_archive_entry_list}")
string(APPEND _archive_entries "\n")

file(WRITE "${_entry_list}" "${_archive_entries}")
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env LC_ALL=C TZ=UTC
        "${TAR_EXECUTABLE}"
        --sort=name
        --mtime=@0
        --owner=0
        --group=0
        --numeric-owner
        --format=ustar
        --mode=u+rwX,go+rX,go-w
        --no-recursion
        -cf "${_temporary_archive}"
        -C "${_prefix_parent}"
        --files-from "${_entry_list}"
    RESULT_VARIABLE _archive_result
    OUTPUT_VARIABLE _archive_stdout
    ERROR_VARIABLE _archive_stderr
)
file(REMOVE "${_entry_list}")
if(NOT _archive_result EQUAL 0 OR
   NOT _archive_stdout STREQUAL "" OR
   NOT EXISTS "${_temporary_archive}" OR
   IS_DIRECTORY "${_temporary_archive}")
    file(REMOVE "${_temporary_archive}")
    message(FATAL_ERROR
        "deterministic distribution archive failed\n"
        "exit: ${_archive_result}\n"
        "stdout: ${_archive_stdout}\nstderr: ${_archive_stderr}")
endif()
file(RENAME "${_temporary_archive}" "${OUTPUT_ARCHIVE}")
file(SHA256 "${OUTPUT_ARCHIVE}" _archive_sha256)
file(WRITE "${_sidecar}.tmp" "${_archive_sha256}\n")
file(RENAME "${_sidecar}.tmp" "${_sidecar}")
