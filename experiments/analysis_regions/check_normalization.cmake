# Pin the observed limitation: promotion creates an untagged induction phi.
# This frozen R1 observation is not an R2 requirement to leave new phi nodes untagged.
foreach(input captured no-debug-captured)
    file(STRINGS "${WORK_DIR}/${input}.ll" original_phis REGEX " = phi ")
    if(original_phis)
        message(FATAL_ERROR "${input}: fixture no longer observes phi creation by mem2reg")
    endif()
endforeach()
foreach(input canonical sealed no-debug-canonical)
    file(STRINGS "${WORK_DIR}/${input}.ll" normalized_phis REGEX " = phi ")
    list(LENGTH normalized_phis phi_count)
    if(NOT phi_count EQUAL 1 OR normalized_phis MATCHES "!yarda.region")
        message(FATAL_ERROR "${input}: expected one normalization-created, untagged phi")
    endif()
endforeach()
# check_map.cmake independently verifies that this loop still has bound=3/index=i+1.
