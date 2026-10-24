file(REMOVE "${OUTPUT_FILE}" "${INPUT_COPY}")
execute_process(
    COMMAND "${BALTAM_IR}" "${M_FILE}" -o "${OUTPUT_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
if(NOT result EQUAL 0 OR NOT stdout STREQUAL "" OR NOT EXISTS "${OUTPUT_FILE}")
    message(FATAL_ERROR "Writing IR to a file failed: ${stderr}")
endif()
file(READ "${OUTPUT_FILE}" ir)
string(FIND "${ir}" "; ModulePath = '${M_FILE}'" path_at)
string(FIND "${ir}" "define @constant(" function_at)
string(FIND "${ir}" "; stage: Non-SSA" stage_at)
if(NOT path_at EQUAL 0 OR function_at LESS 0 OR stage_at LESS 0)
    message(FATAL_ERROR "Output file does not contain the expected IR")
endif()

# 拒绝覆盖输入文件，且失败后仍保留输入内容。
configure_file("${M_FILE}" "${INPUT_COPY}" COPYONLY)
file(READ "${INPUT_COPY}" original)
execute_process(
    COMMAND "${BALTAM_IR}" -o "${INPUT_COPY}" "${INPUT_COPY}"
    RESULT_VARIABLE rejected
    OUTPUT_QUIET
    ERROR_QUIET)
file(READ "${INPUT_COPY}" after)
file(REMOVE "${OUTPUT_FILE}" "${INPUT_COPY}")
if(rejected EQUAL 0 OR NOT after STREQUAL original)
    message(FATAL_ERROR "The input file was overwritten by -o")
endif()
