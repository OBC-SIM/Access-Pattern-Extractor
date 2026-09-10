run("${COMPILER}" "${boundary}" "${WORK_DIR}/explicit-o0.ll" -O0)
expect_snapshot(explicit-o0.ll "${expected}" explicit-o0.txt)

# Optimization/LTO options reach the action guard. All other options are rejected
# by the closed CLI list before Clang runs. Neither path may produce output.
foreach(option -O1 -O2 -O3 -Os -Oz -Ofast -flto "-flto=full" "-flto=thin"
        "-fsanitize=undefined" "-fsanitize=address"
        "-fsanitize-coverage=trace-pc-guard" "-fpass-plugin=${APE_PLUGIN}"
        "-fplugin=${PLUGIN}" -finstrument-functions -fprofile-instr-generate)
    string(MAKE_C_IDENTIFIER "${option}" suffix)
    set(rejected_output "${WORK_DIR}/rejected-${suffix}.ll")
    file(REMOVE "${rejected_output}")
    execute_process(COMMAND "${COMPILER}" "${boundary}" "${rejected_output}" "${option}"
        RESULT_VARIABLE rejected ERROR_VARIABLE diagnostic)
    if(rejected EQUAL 0 OR NOT diagnostic MATCHES "region pipeline requires O0 without LTO")
        message(FATAL_ERROR "unsupported compiler option passed the pipeline gate: ${option}\n${diagnostic}")
    endif()
    if(EXISTS "${rejected_output}")
        message(FATAL_ERROR "rejected pipeline left output: ${option}")
    endif()
endforeach()
