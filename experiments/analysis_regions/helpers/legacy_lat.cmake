# Preserve the observed tagged IR while providing a transport-free copy solely
# for the whole-function control comparison. R2's legacy plugin rejects tags.
function(run_legacy_lat input)
    file(READ "${WORK_DIR}/${input}.ll" ir)
    string(REGEX REPLACE ", !yarda\\.region ![0-9]+" "" ir "${ir}")
    string(REPLACE "\"yarda.region\"=\"APE_ANALYZE\"" "" ir "${ir}")
    file(MAKE_DIRECTORY "${WORK_DIR}/legacy-inputs")
    file(WRITE "${WORK_DIR}/legacy-inputs/${input}.ll" "${ir}")
    run("${OPT}" "-load-pass-plugin=${APE_PLUGIN}" -passes=loop-annotated-trace
        "${WORK_DIR}/legacy-inputs/${input}.ll" -disable-output)
endfunction()
