include_guard(GLOBAL)

function(crankwave_add_web_workbench _wasm_target)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR
                "the interactive workbench must be assembled by Emscripten")
    endif()
    if(NOT TARGET "${_wasm_target}")
        message(FATAL_ERROR
                "workbench assembly requires WASM target '${_wasm_target}'")
    endif()

    set(_output_root "${CMAKE_BINARY_DIR}/workbench")
    set(_stamp "${_output_root}/.assembled")

    file(
        GLOB_RECURSE
        _web_sources
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/web/*"
    )
    file(
        GLOB_RECURSE
        _data_sources
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/data/*"
    )
    file(
        GLOB
        _presentation_assets
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/reference/fixtures/*/presentation/*.wav"
    )
    set(
        _audio_atlas_fixture_root
        "${PROJECT_SOURCE_DIR}/reference/fixtures/audio-atlases"
    )
    file(
        GLOB
        _audio_atlas_fixture_directories
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES true
        "${_audio_atlas_fixture_root}/*"
    )
    set(_audio_atlas_fixture_commands)
    set(_audio_atlas_fixture_files)
    foreach(_audio_atlas_fixture_directory IN LISTS _audio_atlas_fixture_directories)
        if(NOT IS_DIRECTORY "${_audio_atlas_fixture_directory}")
            continue()
        endif()
        if(NOT EXISTS "${_audio_atlas_fixture_directory}/atlas.json")
            message(FATAL_ERROR
                    "audio-atlas fixture '${_audio_atlas_fixture_directory}' is incomplete: atlas.json is missing")
        endif()

        get_filename_component(
            _audio_atlas_fixture_leaf
            "${_audio_atlas_fixture_directory}"
            NAME
        )
        file(
            GLOB_RECURSE
            _audio_atlas_fixture_directory_files
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES false
            "${_audio_atlas_fixture_directory}/*"
        )
        list(
            APPEND
            _audio_atlas_fixture_files
            ${_audio_atlas_fixture_directory_files}
        )
        list(
            APPEND
            _audio_atlas_fixture_commands
            COMMAND
                "${CMAKE_COMMAND}" -E copy_directory
                "${_audio_atlas_fixture_directory}"
                "${_output_root}/packages/${_audio_atlas_fixture_leaf}"
        )
    endforeach()
    set(_reference_asset_commands)
    foreach(_reference_asset IN LISTS _presentation_assets)
        file(
            RELATIVE_PATH
            _reference_relative_path
            "${PROJECT_SOURCE_DIR}"
            "${_reference_asset}"
        )
        get_filename_component(
            _reference_relative_directory
            "${_reference_relative_path}"
            DIRECTORY
        )
        list(
            APPEND
            _reference_asset_commands
            COMMAND
                "${CMAKE_COMMAND}" -E make_directory
                "${_output_root}/${_reference_relative_directory}"
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${_reference_asset}"
                "${_output_root}/${_reference_relative_path}"
        )
    endforeach()

    add_custom_command(
        OUTPUT "${_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${_output_root}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output_root}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output_root}/packages"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory
            "${PROJECT_SOURCE_DIR}/web"
            "${_output_root}/web"
        COMMAND
            "${CMAKE_COMMAND}" -E remove_directory
            "${_output_root}/web/tests"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory
            "${PROJECT_SOURCE_DIR}/data"
            "${_output_root}/data"
        ${_audio_atlas_fixture_commands}
        ${_reference_asset_commands}
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${_wasm_target}>"
            "${_output_root}/web/crankwave.js"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:${_wasm_target}>/crankwave.wasm"
            "${_output_root}/web/crankwave.wasm"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
        DEPENDS
            "${_wasm_target}"
            ${_web_sources}
            ${_data_sources}
            ${_audio_atlas_fixture_files}
            ${_presentation_assets}
        COMMENT "Assembling the isolated browser workbench"
        VERBATIM
    )

    add_custom_target(
        crankwave_web_workbench
        DEPENDS "${_stamp}"
    )
endfunction()
