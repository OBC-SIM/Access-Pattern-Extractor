file(MAKE_DIRECTORY "${WORK_DIR}/path with spaces")
set(source "${SOURCE_DIR}/experiments/analysis_regions/fixtures/boundary.c")
set(output "${WORK_DIR}/result.json")

execute_process(COMMAND "${COMPILER}" "${source}" "${output}"
    RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
if(NOT status EQUAL 0 OR NOT EXISTS "${output}")
    message(FATAL_ERROR "region frontend failed: ${diagnostic}")
endif()
file(READ "${output}" first)
execute_process(COMMAND "${COMPILER}" "${source}" "${output}"
    RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
file(READ "${output}" second)
if(NOT status EQUAL 0 OR NOT first STREQUAL second)
    message(FATAL_ERROR "region MAP is not repeatable: ${diagnostic}")
endif()

# An invalid invocation must preserve the last successfully published document.
foreach(option -O2 -flto -fsanitize=undefined -Xclang @options)
    execute_process(COMMAND "${COMPILER}" "${source}" "${output}" "${option}"
        RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
    file(READ "${output}" retained)
    if(NOT status EQUAL 1 OR NOT retained STREQUAL first OR
       NOT diagnostic MATCHES "unsupported region compiler option")
        message(FATAL_ERROR "compiler option rejection failed: ${option}: ${diagnostic}")
    endif()
endforeach()
execute_process(COMMAND "${COMPILER}"
    "${SOURCE_DIR}/experiments/analysis_regions/fixtures/dynamic_bound.c" "${output}"
    RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
file(READ "${output}" retained)
if(NOT status EQUAL 1 OR NOT retained STREQUAL first OR
   NOT diagnostic MATCHES "region loop bound")
    message(FATAL_ERROR "dynamic bound was not rejected before publication: ${diagnostic}")
endif()

file(WRITE "${WORK_DIR}/boundary.h"
    "void header_region(void) {\n#pragma APE_ANALYZE_BEGIN\n#pragma APE_ANALYZE_END\n}\n")
file(WRITE "${WORK_DIR}/header.c" "#include \"boundary.h\"\n")
file(REMOVE "${WORK_DIR}/header.json")
execute_process(COMMAND "${COMPILER}" "${WORK_DIR}/header.c" "${WORK_DIR}/header.json"
    RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
if(NOT status EQUAL 1 OR EXISTS "${WORK_DIR}/header.json" OR
   NOT diagnostic MATCHES "literal main-file")
    message(FATAL_ERROR "header boundary rejection failed: ${diagnostic}")
endif()

configure_file("${source}" "${WORK_DIR}/path with spaces/source.c" COPYONLY)
execute_process(COMMAND "${COMPILER}" "${WORK_DIR}/path with spaces/source.c"
    "${WORK_DIR}/path with spaces/result.json"
    RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "source/output paths with spaces failed: ${diagnostic}")
endif()

if(UNIX)
    execute_process(COMMAND sh
        "${SOURCE_DIR}/experiments/analysis_regions/helpers/limit_output.sh"
        "${COMPILER}" "${source}" "${WORK_DIR}/limited.json"
        RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
    file(GLOB leftovers "${WORK_DIR}/limited.json*")
    if(NOT status EQUAL 1 OR leftovers OR NOT diagnostic MATCHES "cannot write MAP")
        message(FATAL_ERROR "output failure did not cleanly reject publication: ${status}: ${diagnostic}")
    endif()
endif()
