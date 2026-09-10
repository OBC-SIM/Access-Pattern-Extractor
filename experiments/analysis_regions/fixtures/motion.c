int shared_value, before, inside, after;

#ifdef OPAQUE_MARKERS
extern void region_begin(void);
extern void region_end(void);
#endif

/**
 * @brief Expose load merging across source boundaries under optimization.
 * @return Nothing; the fixture is compiled but never executed.
 */
void motion_probe(void) {
    before = shared_value;
#ifdef OPAQUE_MARKERS
    region_begin();
#else
#pragma APE_ANALYZE_BEGIN
#endif
    inside = shared_value;
#ifdef OPAQUE_MARKERS
    region_end();
#else
#pragma APE_ANALYZE_END
#endif
    after = shared_value;
}
