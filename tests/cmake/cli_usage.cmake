if(NOT DEFINED PROGRAM)
    message(FATAL_ERROR "PROGRAM was not provided")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR was not provided")
endif()

execute_process(
    COMMAND "${PROGRAM}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
)

if(result EQUAL 0)
    message(FATAL_ERROR "The CLI unexpectedly succeeded without an input file")
endif()

string(CONCAT all_output "${output}" "${error_output}")
if(NOT all_output MATCHES "Usage:")
    message(FATAL_ERROR "The CLI did not print its usage text. Output: ${all_output}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(input "${WORK_DIR}/input.snbt")
set(binary_output "${WORK_DIR}/output.dat")
set(snbt_output "${WORK_DIR}/roundtrip.snbt")
file(WRITE "${input}" "{Data:{Value:1,Name:\"Steve\"}}\n")

execute_process(
    COMMAND "${PROGRAM}" "${input}" --edit Data/Value 42 --output "${binary_output}"
    RESULT_VARIABLE edit_result
    OUTPUT_VARIABLE edit_output
    ERROR_VARIABLE edit_error
)
if(NOT edit_result EQUAL 0 OR NOT EXISTS "${binary_output}")
    message(FATAL_ERROR "SNBT-to-binary edit failed: ${edit_output}${edit_error}")
endif()

execute_process(
    COMMAND "${PROGRAM}" "${binary_output}" --validate
    RESULT_VARIABLE validate_result
    OUTPUT_VARIABLE validate_output
    ERROR_VARIABLE validate_error
)
if(NOT validate_result EQUAL 0 OR NOT validate_output MATCHES "Valid NBT document")
    message(FATAL_ERROR "Edited binary validation failed: ${validate_output}${validate_error}")
endif()

execute_process(
    COMMAND "${PROGRAM}" "${binary_output}" --snbt "${snbt_output}"
    RESULT_VARIABLE export_result
    OUTPUT_VARIABLE export_output
    ERROR_VARIABLE export_error
)
if(NOT export_result EQUAL 0 OR NOT EXISTS "${snbt_output}")
    message(FATAL_ERROR "Binary-to-SNBT export failed: ${export_output}${export_error}")
endif()
file(READ "${snbt_output}" roundtrip)
if(NOT roundtrip MATCHES "\"Value\"[ \t\r\n]*:[ \t\r\n]*42")
    message(FATAL_ERROR "The edited value did not survive the round trip: ${roundtrip}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
