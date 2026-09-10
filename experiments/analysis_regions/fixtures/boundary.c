#define APE_ANALYZE __attribute__((annotate("ape.analyze")))

int before[8], inside[8], after[8];

/**
 * @brief Exercise outside dependencies and a complete selected loop.
 * @return Nothing; the fixture is compiled but never executed.
 */
APE_ANALYZE void region_probe(void) {
    const int bound = 3;
    const int offset = 1;
    before[0] = 11;
#pragma APE_ANALYZE_BEGIN
    for (int i = 0; i < bound; ++i)
        inside[i + offset] += 1;
#pragma APE_ANALYZE_END
    after[0] = 22;
}
