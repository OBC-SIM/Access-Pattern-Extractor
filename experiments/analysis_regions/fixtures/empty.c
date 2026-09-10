int before, after;

/**
 * @brief Preserve an empty scope between observable outside accesses.
 * @return Nothing; the fixture is compiled but never executed.
 */
void empty_probe(void) {
    before = 1;
#pragma APE_ANALYZE_BEGIN
#pragma APE_ANALYZE_END
    after = 2;
}
