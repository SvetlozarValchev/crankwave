include_guard(GLOBAL)

function(engine_sim_offline_add_web_workbench _wasm_target)
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
    set(_reference_directory
        "${_output_root}/reference/fixtures/bmw-m52b28-p18/presentation")
    set(_reference_ir
        "${PROJECT_SOURCE_DIR}/reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav")

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

    add_custom_command(
        OUTPUT "${_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${_output_root}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output_root}"
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
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${_reference_directory}"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "${_reference_ir}"
            "${_reference_directory}/smooth_39.wav"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${_wasm_target}>"
            "${_output_root}/web/engine-sim-offline.js"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:${_wasm_target}>/engine-sim-offline.wasm"
            "${_output_root}/web/engine-sim-offline.wasm"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
        DEPENDS
            "${_wasm_target}"
            ${_web_sources}
            ${_data_sources}
            "${_reference_ir}"
        COMMENT "Assembling the isolated browser workbench"
        VERBATIM
    )

    add_custom_target(
        engine_sim_offline_web_workbench
        DEPENDS "${_stamp}"
    )
endfunction()
