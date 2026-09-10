# This is deliberately an unsafe comparison, not a supported region pipeline.
set(motion "${SOURCE_DIR}/fixtures/motion.c")
run(${compile_o0} "-fplugin=${PLUGIN}" "${motion}" -o motion-o0.ll)
run("${PROBE}" capture "${WORK_DIR}/motion-o0.ll" "${WORK_DIR}/motion-captured.ll")
run("${OPT}" "-passes=${canonical}" -S motion-captured.ll -o motion-canonical.ll)
expect_snapshot(motion-canonical.ll
    "function motion_probe\nload shared_value outside\nstore before outside\nload shared_value region\nstore inside region\nload shared_value outside\nstore after outside\n"
    motion-canonical.txt)

run("${CLANG}" -O2 -g -emit-llvm -S "-fplugin=${PLUGIN}" "${motion}" -o motion-o2.ll)
run("${PROBE}" capture "${WORK_DIR}/motion-o2.ll" "${WORK_DIR}/motion-o2-captured.ll")
expect_snapshot(motion-o2-captured.ll
    "function motion_probe\nload shared_value outside\nstore before outside\nstore inside region\nstore after outside\n"
    motion-o2.txt)

# Ordinary opaque marker calls also change optimized visible-memory accesses.
run("${CLANG}" -O2 -g -emit-llvm -S "${motion}" -o motion-native-o2.ll)
run("${CLANG}" -O2 -g -DOPAQUE_MARKERS -emit-llvm -S "${motion}" -o motion-opaque-o2.ll)
expect_snapshot(motion-native-o2.ll
    "function motion_probe\nload shared_value outside\nstore before outside\nstore inside outside\nstore after outside\n"
    motion-native-o2.txt)
expect_snapshot(motion-opaque-o2.ll
    "function motion_probe\nload shared_value outside\nstore before outside\nload shared_value outside\nstore inside outside\nload shared_value outside\nstore after outside\n"
    motion-opaque-o2.txt)

# Instruction metadata alone does not make arbitrary optimization safe either.
run("${OPT}" -passes=default<O2> -S motion-captured.ll -o motion-post-o2.ll)
expect_snapshot(motion-post-o2.ll
    "function motion_probe\nload shared_value outside\nstore before outside\nstore inside region\nstore after outside\n"
    motion-post-o2.txt)
