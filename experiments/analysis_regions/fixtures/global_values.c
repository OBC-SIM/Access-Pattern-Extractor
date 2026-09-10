#define APE_ANALYZE __attribute__((annotate("ape.analyze")))

const int bound = 3;
const int offset = 1;
int external_value = 7;
int before[8], inside[8], after[8];

/**
 * @brief Distinguish outside value reuse from an actual selected global load.
 * @return Nothing; the fixture is compiled but never executed.
 */
APE_ANALYZE void global_values_probe(void) {
    const int cached = external_value;
    before[0] = 11;
#pragma APE_ANALYZE_BEGIN
    for (int i = 0; i < bound; ++i) {
        const int sampled = external_value;
        inside[i + offset] += cached + sampled;
    }
#pragma APE_ANALYZE_END
    after[0] = 22;
}
