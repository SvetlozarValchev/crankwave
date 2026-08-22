include_guard(GLOBAL)

function(_crankwave_numeric_policy_configurations _output)
    set(_configurations Debug Release RelWithDebInfo MinSizeRel)
    if(CMAKE_CONFIGURATION_TYPES)
        list(APPEND _configurations ${CMAKE_CONFIGURATION_TYPES})
    endif()
    if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
        list(APPEND _configurations "${CMAKE_BUILD_TYPE}")
    endif()
    list(REMOVE_DUPLICATES _configurations)
    set(${_output} "${_configurations}" PARENT_SCOPE)
endfunction()

function(crankwave_define_renderer_numeric_policy)
    if(TARGET crankwave_renderer_numeric_policy)
        message(FATAL_ERROR "renderer numeric policy was already defined")
    endif()

    set(
        _policy_flags
        -march=x86-64
        -mtune=generic
        -mfpmath=sse
        -mno-avx
        -mno-avx2
        -mno-fma
        -fno-lto
        -fexcess-precision=standard
        -fno-fast-math
        -ffp-contract=off
    )
    string(JOIN " " _policy_flag_text ${_policy_flags})
    set(_policy_admitted 0)
    set(_compiler_frontend_admitted 0)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # CMake 3.21-3.25 leave FRONTEND_VARIANT empty for GCC. Compiler ID is
        # already unambiguous here; the frontend distinction matters only for
        # Clang versus clang-cl.
        set(_compiler_frontend_admitted 1)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
           CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        set(_compiler_frontend_admitted 1)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND
       _compiler_frontend_admitted)
        set(_policy_admitted 1)
    endif()

    set(
        _generated_directory
        "${PROJECT_BINARY_DIR}/generated/crankwave_generated"
    )
    file(MAKE_DIRECTORY "${_generated_directory}")
    set(CRANKWAVE_RENDERER_NUMERIC_POLICY_ADMITTED
        "${_policy_admitted}")
    set(CRANKWAVE_RENDERER_NUMERIC_POLICY_FLAGS
        "${_policy_flag_text}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RendererNumericPolicyGenerated.hpp.in"
        "${_generated_directory}/renderer_numeric_policy_generated.hpp"
        @ONLY
    )

    add_library(crankwave_renderer_numeric_policy INTERFACE)
    set_property(
        TARGET crankwave_renderer_numeric_policy
        PROPERTY CRANKWAVE_NUMERIC_POLICY_ADMITTED "${_policy_admitted}"
    )
    set_property(
        TARGET crankwave_renderer_numeric_policy
        PROPERTY CRANKWAVE_NUMERIC_POLICY_FLAG_TEXT "${_policy_flag_text}"
    )
    target_include_directories(
        crankwave_renderer_numeric_policy
        INTERFACE $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    )
    if(_policy_admitted)
        # Keep the exact set as one shell-parsed group so it remains a contiguous
        # target-owned tail after ordinary CMake compile flags.
        set(
            _policy_compile_option
            "$<$<COMPILE_LANGUAGE:CXX>:SHELL:${_policy_flag_text}>"
        )
        target_compile_options(
            crankwave_renderer_numeric_policy
            INTERFACE "${_policy_compile_option}"
        )
        set_property(
            TARGET crankwave_renderer_numeric_policy
            PROPERTY CRANKWAVE_NUMERIC_POLICY_COMPILE_OPTION
                     "${_policy_compile_option}"
        )
    endif()
endfunction()

function(crankwave_renderer_numeric_policy_is_admitted _output)
    if(NOT TARGET crankwave_renderer_numeric_policy)
        message(FATAL_ERROR "renderer numeric policy has not been defined")
    endif()
    get_target_property(
        _admitted
        crankwave_renderer_numeric_policy
        CRANKWAVE_NUMERIC_POLICY_ADMITTED
    )
    set(${_output} "${_admitted}" PARENT_SCOPE)
endfunction()

function(crankwave_enable_renderer_numeric_policy _target)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR "renderer numeric policy requires an existing target")
    endif()
    if(NOT TARGET crankwave_renderer_numeric_policy)
        message(FATAL_ERROR "renderer numeric policy has not been defined")
    endif()
    target_link_libraries(
        "${_target}"
        PRIVATE crankwave_renderer_numeric_policy
    )
    set_property(
        TARGET "${_target}"
        PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE
    )
    _crankwave_numeric_policy_configurations(_configurations)
    foreach(_configuration IN LISTS _configurations)
        string(TOUPPER "${_configuration}" _configuration_upper)
        set_property(
            TARGET "${_target}"
            PROPERTY "INTERPROCEDURAL_OPTIMIZATION_${_configuration_upper}" FALSE
        )
    endforeach()
endfunction()

function(crankwave_finalize_renderer_numeric_policy _target)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR "renderer numeric policy requires an existing target")
    endif()
    crankwave_renderer_numeric_policy_is_admitted(_admitted)
    if(_admitted)
        target_link_options("${_target}" PRIVATE -fno-lto)
    endif()
endfunction()

function(crankwave_assert_renderer_numeric_policy _target)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR "required numeric-policy target '${_target}' is missing")
    endif()

    get_target_property(_links "${_target}" LINK_LIBRARIES)
    list(FIND _links crankwave_renderer_numeric_policy _policy_index)
    if(_policy_index EQUAL -1)
        message(FATAL_ERROR
                "target '${_target}' is outside the renderer numeric-policy closure")
    endif()

    get_target_property(_ipo "${_target}" INTERPROCEDURAL_OPTIMIZATION)
    if(NOT "${_ipo}" STREQUAL "FALSE")
        message(FATAL_ERROR "target '${_target}' did not disable IPO")
    endif()
    _crankwave_numeric_policy_configurations(_configurations)
    foreach(_configuration IN LISTS _configurations)
        string(TOUPPER "${_configuration}" _configuration_upper)
        get_target_property(
            _configuration_ipo
            "${_target}"
            "INTERPROCEDURAL_OPTIMIZATION_${_configuration_upper}"
        )
        if(NOT "${_configuration_ipo}" STREQUAL "FALSE")
            message(FATAL_ERROR
                    "target '${_target}' did not disable IPO for ${_configuration}")
        endif()
    endforeach()

    crankwave_renderer_numeric_policy_is_admitted(_admitted)
    get_target_property(
        _observed_options
        crankwave_renderer_numeric_policy
        INTERFACE_COMPILE_OPTIONS
    )
    get_target_property(
        _expected_option
        crankwave_renderer_numeric_policy
        CRANKWAVE_NUMERIC_POLICY_COMPILE_OPTION
    )
    if(_admitted AND NOT "${_observed_options}" STREQUAL "${_expected_option}")
        message(FATAL_ERROR "renderer numeric-policy option tail changed")
    endif()
    if(NOT _admitted AND _observed_options)
        message(FATAL_ERROR "unsupported build received renderer numeric options")
    endif()
endfunction()

function(crankwave_assert_final_renderer_numeric_policy _target)
    crankwave_assert_renderer_numeric_policy("${_target}")
    crankwave_renderer_numeric_policy_is_admitted(_admitted)
    if(_admitted)
        get_target_property(_link_options "${_target}" LINK_OPTIONS)
        list(FIND _link_options -fno-lto _no_lto_index)
        if(_no_lto_index EQUAL -1)
            message(FATAL_ERROR
                    "final renderer target '${_target}' did not disable link LTO")
        endif()
    endif()
endfunction()
