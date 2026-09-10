#define APE_ANALYZE __attribute__((annotate("ape.analyze")))

extern int bound;
int inside[8];

/**
 * @brief Expose a selected bound load that R2 must reject as unresolved.
 * @return Nothing; R1 observes IR only and never executes or exports this loop.
 */
APE_ANALYZE void dynamic_bound_probe(void) {
#pragma APE_ANALYZE_BEGIN
    for (int i = 0; i < bound; ++i)
        inside[i] += 1;
#pragma APE_ANALYZE_END
}
