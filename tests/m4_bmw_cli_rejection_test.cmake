if(NOT DEFINED LISTENING_EXE OR NOT DEFINED TORQUE_EXE OR
   NOT DEFINED TEST_BINARY_DIR)
    message(FATAL_ERROR "M4 CLI rejection test inputs are incomplete")
endif()

set(scratch "${TEST_BINARY_DIR}/m4-cli-rejection-scratch")
file(REMOVE_RECURSE "${scratch}")
file(MAKE_DIRECTORY "${scratch}")

execute_process(
    COMMAND "${LISTENING_EXE}" held "${scratch}" "${scratch}/unused"
    WORKING_DIRECTORY "${scratch}"
    RESULT_VARIABLE generic_held_result
    OUTPUT_VARIABLE generic_held_stdout
    ERROR_VARIABLE generic_held_stderr
)
if(generic_held_result EQUAL 0 OR
   NOT generic_held_stderr MATCHES "listening mode must be exactly one of")
    message(FATAL_ERROR "deleted generic held token did not fail closed")
endif()

execute_process(
    COMMAND "${LISTENING_EXE}" unknown "${scratch}" "${scratch}/unused"
    WORKING_DIRECTORY "${scratch}"
    RESULT_VARIABLE unknown_result
    OUTPUT_VARIABLE unknown_stdout
    ERROR_VARIABLE unknown_stderr
)
if(unknown_result EQUAL 0 OR
   NOT unknown_stderr MATCHES "listening mode must be exactly one of")
    message(FATAL_ERROR "unknown listening token did not fail closed")
endif()

foreach(rejected_alias IN ITEMS
        rpm700-throttle0
        rpm1500-throttle0p10
        held-idle-region
        held-low-load)
    execute_process(
        COMMAND
            "${LISTENING_EXE}"
            "${rejected_alias}"
            "${scratch}"
            "${scratch}/unused"
        WORKING_DIRECTORY "${scratch}"
        RESULT_VARIABLE rejected_alias_result
        OUTPUT_VARIABLE rejected_alias_stdout
        ERROR_VARIABLE rejected_alias_stderr
    )
    if(rejected_alias_result EQUAL 0 OR
       NOT rejected_alias_stderr MATCHES
           "listening mode must be exactly one of")
        message(FATAL_ERROR
            "listening CLI accepted noncanonical alias ${rejected_alias}")
    endif()
endforeach()

foreach(held_token IN ITEMS
        held-rpm1500-throttle0p85
        held-rpm3000-throttle0p25
        held-rpm3000-throttle0p85
        held-rpm6500-throttle0p85
        held-idle-region-rpm700-throttle0
        held-low-load-rpm1500-throttle0p10)
    execute_process(
        COMMAND
            "${LISTENING_EXE}"
            "${held_token}"
            "${scratch}"
            "${scratch}/--help"
        WORKING_DIRECTORY "${scratch}"
        RESULT_VARIABLE listening_option_path_result
        OUTPUT_VARIABLE listening_option_path_stdout
        ERROR_VARIABLE listening_option_path_stderr
    )
    if(listening_option_path_result EQUAL 0 OR
       NOT listening_option_path_stderr MATCHES
           "output publication name must not begin with '-'")
        message(FATAL_ERROR
            "listening CLI token ${held_token} did not reach output preflight")
    endif()
    if(EXISTS "${scratch}/--help")
        message(FATAL_ERROR
            "listening CLI token ${held_token} created an option-shaped output")
    endif()
endforeach()

execute_process(
    COMMAND "${TORQUE_EXE}" "${scratch}/--help"
    WORKING_DIRECTORY "${scratch}"
    RESULT_VARIABLE torque_option_path_result
    OUTPUT_VARIABLE torque_option_path_stdout
    ERROR_VARIABLE torque_option_path_stderr
)
if(torque_option_path_result EQUAL 0 OR
   NOT torque_option_path_stderr MATCHES
       "publication directory name must not begin with '-'")
    message(FATAL_ERROR "torque CLI accepted an option-shaped output path")
endif()
if(EXISTS "${scratch}/--help")
    message(FATAL_ERROR "torque CLI created an option-shaped output directory")
endif()

file(REMOVE_RECURSE "${scratch}")
