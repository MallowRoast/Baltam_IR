execute_process(
    COMMAND "${BALTAM_IR}" "${M_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "baltam_ir failed: ${error}")
endif()

string(FIND "${output}" "define @multi_function(slot0 x) -> (slot1 y) {" main)
string(FIND "${output}" "define @twice(slot0 x) -> (slot1 y) {" twice)
string(FIND "${output}" "define @add_one(slot0 x) -> (slot1 y) {" add_one)
if(main LESS 0 OR twice LESS main OR add_one LESS twice)
    message(FATAL_ERROR "Missing main or local function IR:\n${output}")
endif()

string(REGEX MATCHALL "define @[^\n]+" functions "${output}")
list(LENGTH functions count)
if(NOT count EQUAL 3)
    message(FATAL_ERROR "Expected three functions, got ${count}:\n${output}")
endif()

string(FIND "${output}" "; ModulePath = '${M_FILE}'" module_path)
string(FIND "${output}" ";   slot0 = x" input_slot)
string(FIND "${output}" ";   slot1 = y" output_slot)
if(NOT module_path EQUAL 0 OR input_slot LESS main OR output_slot LESS input_slot)
    message(FATAL_ERROR "Missing module header or slot names:\n${output}")
endif()
