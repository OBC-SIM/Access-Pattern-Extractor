file(MAKE_DIRECTORY "${WORK_DIR}")
set(attribute [=[
define void @kernel() #0 { ret void }
attributes #0 = { "yarda.region"="APE_ANALYZE" }
]=])
set(metadata [=[
define void @kernel() { ret void, !yarda.region !0 }
!0 = !{!"APE_ANALYZE"}
]=])
set(marker [=[
@begin = constant [22 x i8] c"yarda.region.begin.v1\00"
declare i32 @llvm.annotation.i32(i32, i8*, i8*, i32)
define void @kernel() {
  %b = call i32 @llvm.annotation.i32(i32 0, i8* getelementptr
    ([22 x i8], [22 x i8]* @begin, i32 0, i32 0), i8* null, i32 1)
  ret void
}
]=])

foreach(kind attribute metadata marker)
    set(source "${WORK_DIR}/${kind}.ll")
    set(output "${WORK_DIR}/${kind}-output.ll")
    set(lat "${WORK_DIR}/${kind}_ape.json")
    file(WRITE "${source}" "${${kind}}")
    file(REMOVE "${output}" "${lat}")
    execute_process(COMMAND "${OPT}" "-load-pass-plugin=${PLUGIN}"
        -passes=loop-annotated-trace "${source}" -S -o "${output}"
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
    if(NOT status EQUAL 1 OR EXISTS "${output}" OR EXISTS "${lat}" OR
       NOT diagnostic MATCHES "region transport requires yarda_region_lat")
        message(FATAL_ERROR "legacy ${kind} rejection/cleanup failed: ${status}: ${diagnostic}")
    endif()
endforeach()
