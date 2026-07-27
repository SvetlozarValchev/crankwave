if(NOT DEFINED CLI_EXECUTABLE)
    message(FATAL_ERROR "CLI_EXECUTABLE is required")
endif()

function(run_cli result_var stdout_var stderr_var)
    execute_process(
        COMMAND "${CLI_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE standard_out
        ERROR_VARIABLE standard_error
    )
    set(${result_var} "${result}" PARENT_SCOPE)
    set(${stdout_var} "${standard_out}" PARENT_SCOPE)
    set(${stderr_var} "${standard_error}" PARENT_SCOPE)
endfunction()

run_cli(result standard_out standard_error --help)
if(NOT result EQUAL 0 OR NOT standard_out MATCHES "Usage:" OR standard_error)
    message(FATAL_ERROR "--help process contract failed")
endif()

run_cli(result standard_out standard_error --version)
if(NOT result EQUAL 0 OR
   NOT standard_out MATCHES "^engine-sim-offline development" OR
   standard_error)
    message(FATAL_ERROR "--version process contract failed")
endif()

run_cli(result standard_out standard_error render)
if(NOT result EQUAL 69 OR standard_out OR
   NOT standard_error MATCHES "no serialized CLI input contract")
    message(FATAL_ERROR "render process contract failed")
endif()

run_cli(result standard_out standard_error)
if(NOT result EQUAL 64 OR standard_out OR
   NOT standard_error MATCHES "missing command")
    message(FATAL_ERROR "missing-command process contract failed")
endif()
