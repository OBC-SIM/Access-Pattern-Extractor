cmake_minimum_required(VERSION 3.20)

file(MAKE_DIRECTORY "${WORK_DIR}")
set(selected [=[
void selected(void) {
#pragma APE_ANALYZE_BEGIN
  a[0]++;
#pragma APE_ANALYZE_END
}
]=])
file(WRITE "${WORK_DIR}/valid.c" "int a[8];\n${selected}")
set(output "${WORK_DIR}/result.json")
execute_process(COMMAND "${COMPILER}" "${WORK_DIR}/valid.c" "${output}"
    RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "selection baseline failed: ${diagnostic}")
endif()
file(READ "${output}" baseline)

function(expect_rejected source reason)
    execute_process(COMMAND "${COMPILER}" "${WORK_DIR}/${source}" "${output}"
        RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
    file(READ "${output}" retained)
    if(NOT status EQUAL 1 OR NOT diagnostic MATCHES "${reason}" OR
       NOT retained STREQUAL baseline)
        message(FATAL_ERROR "selection rejection/publication failed: ${source}: ${status}: ${diagnostic}")
    endif()
endfunction()

file(WRITE "${WORK_DIR}/plain.c"
    "int a[8], n; void plain(void) {if(n) a[0]=1; else a[1]=2;}\n")
expect_rejected(plain.c "plain: unsupported control flow")
file(WRITE "${WORK_DIR}/conditional.h"
    "int a[8], n; void helper(void) {if(n) a[0]++;}\n")
file(WRITE "${WORK_DIR}/included.c" "#include \"conditional.h\"\n")
expect_rejected(included.c "helper: unsupported control flow")

file(WRITE "${WORK_DIR}/mixed.c" "#include \"conditional.h\"\n${selected}")
execute_process(COMMAND "${COMPILER}" "${WORK_DIR}/mixed.c" "${WORK_DIR}/mixed.json"
    RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "excluded header function rejected: ${diagnostic}")
endif()
file(READ "${WORK_DIR}/mixed.json" mixed)
string(JSON count LENGTH "${mixed}" functions)
string(JSON name GET "${mixed}" functions 0 function)
if(NOT count EQUAL 1 OR NOT name STREQUAL "selected")
    message(FATAL_ERROR "source validation and export selections disagree")
endif()

file(WRITE "${WORK_DIR}/named.h"
    "int a, n; void helper(void) __asm__(\"renamed\"); void helper(void) {if(n) a++;}\n")
file(WRITE "${WORK_DIR}/named.c" "#include \"named.h\"\n")
expect_rejected(named.c "custom assembler symbols")
