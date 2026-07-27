include_guard(GLOBAL)

function(_engine_sim_offline_renderer_stamp_inputs_representable _output)
    set(_representable TRUE)
    foreach(
        _variable_name
        IN ITEMS
            CMAKE_CXX_COMPILER
            CMAKE_CXX_COMPILER_ARG1
            CMAKE_CXX_FLAGS
            CMAKE_CXX_COMPILER_TARGET
    )
        if("${${_variable_name}}" MATCHES "[;\r\n]")
            set(_representable FALSE)
        endif()
    endforeach()

    if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _configuration_upper)
        if("${CMAKE_CXX_FLAGS_${_configuration_upper}}" MATCHES "[;\r\n]")
            set(_representable FALSE)
        endif()
    endif()
    set(${_output} "${_representable}" PARENT_SCOPE)
endfunction()

function(engine_sim_offline_add_renderer_source_stamp _target)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR "renderer source stamp requires an existing target")
    endif()
    find_package(Git QUIET)

    set(_is_multi_config FALSE)
    if(CMAKE_CONFIGURATION_TYPES)
        set(_is_multi_config TRUE)
    endif()

    set(_configuration_flags "")
    if(NOT _is_multi_config AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _configuration_upper)
        set(_configuration_flags
            "${CMAKE_CXX_FLAGS_${_configuration_upper}}")
    endif()

    set(_has_compiler_launcher FALSE)
    if(DEFINED CMAKE_CXX_COMPILER_LAUNCHER AND
       NOT "${CMAKE_CXX_COMPILER_LAUNCHER}" STREQUAL "")
        set(_has_compiler_launcher TRUE)
    endif()
    get_property(_global_rule_launcher GLOBAL PROPERTY RULE_LAUNCH_COMPILE)
    get_property(_directory_rule_launcher DIRECTORY PROPERTY RULE_LAUNCH_COMPILE)
    if(NOT "${_global_rule_launcher}" STREQUAL "" OR
       NOT "${_directory_rule_launcher}" STREQUAL "")
        set(_has_compiler_launcher TRUE)
    endif()

    _engine_sim_offline_renderer_stamp_inputs_representable(
        _query_inputs_representable
    )
    set(_toolchain_query_permitted TRUE)
    if(NOT PROJECT_IS_TOP_LEVEL OR _is_multi_config OR
       NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Release" OR
       NOT "${CMAKE_CXX_COMPILER_ARG1}" STREQUAL "" OR
       NOT "${CMAKE_CXX_FLAGS}" STREQUAL "" OR
       NOT "${CMAKE_CXX_COMPILER_TARGET}" STREQUAL "" OR
       NOT "${_configuration_flags}" STREQUAL "-O3 -DNDEBUG" OR
       _has_compiler_launcher OR NOT _query_inputs_representable)
        set(_toolchain_query_permitted FALSE)
    endif()

    # Avoid putting an unrepresentable CMake list or control character into the
    # custom command. The generator will fail closed from the permission bit.
    if(_query_inputs_representable)
        set(_compiler_executable "${CMAKE_CXX_COMPILER}")
        set(_compiler_arg1 "${CMAKE_CXX_COMPILER_ARG1}")
        set(_global_flags "${CMAKE_CXX_FLAGS}")
        set(_configured_target "${CMAKE_CXX_COMPILER_TARGET}")
    else()
        set(_compiler_executable "")
        set(_compiler_arg1 "")
        set(_global_flags "")
        set(_configuration_flags "")
        set(_configured_target "")
    endif()

    set(
        _generated_header
        "${PROJECT_BINARY_DIR}/generated/engine_sim_offline_generated/renderer_source_stamp_generated.hpp"
    )
    add_custom_target(
        engine_sim_offline_generate_renderer_source_stamp
        COMMAND
            ${CMAKE_COMMAND}
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DOUTPUT_HEADER=${_generated_header}"
            "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
            "-DCOMPILER_EXECUTABLE=${_compiler_executable}"
            "-DCOMPILER_ARG1=${_compiler_arg1}"
            "-DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
            "-DCOMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}"
            "-DCONFIGURED_TARGET=${_configured_target}"
            "-DCXX_FLAGS=${_global_flags}"
            "-DCXX_CONFIG_FLAGS=${_configuration_flags}"
            "-DIS_MULTI_CONFIG=${_is_multi_config}"
            "-DTOOLCHAIN_QUERY_PERMITTED=${_toolchain_query_permitted}"
            "-DSYSTEM_NAME=${CMAKE_SYSTEM_NAME}"
            -P "${PROJECT_SOURCE_DIR}/cmake/GenerateRendererSourceStamp.cmake"
        BYPRODUCTS "${_generated_header}"
        VERBATIM
    )

    target_sources(
        "${_target}"
        PRIVATE "${PROJECT_SOURCE_DIR}/src/determinism/renderer_source_stamp.cpp"
    )
    add_dependencies(
        "${_target}"
        engine_sim_offline_generate_renderer_source_stamp
    )

    target_include_directories(
        "${_target}"
        BEFORE PRIVATE $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    )
endfunction()
