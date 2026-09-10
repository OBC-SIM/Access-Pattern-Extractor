#define ANALYZE __attribute__((annotate("ape.analyze")))

int shared[16] __attribute__((aligned(32)));

/** @brief Whole-function control task. @return Nothing. */
ANALYZE void whole(void) { shared[0]++; }

/** @brief Region with outside references to the same cache line. @return
 * Nothing. */
void first(void)
{
  shared[0] = 1;
#pragma APE_ANALYZE_BEGIN
  shared[0]++;
#pragma APE_ANALYZE_END
  shared[0] = 2;
}

/** @brief Independent second region on the same cache line. @return Nothing. */
void second(void)
{
#pragma APE_ANALYZE_BEGIN
  shared[0]++;
#pragma APE_ANALYZE_END
}

/** @brief Preserve an empty independent cold task. @return Nothing. */
void empty(void)
{
#pragma APE_ANALYZE_BEGIN
#pragma APE_ANALYZE_END
}

/** @brief Native linked fixture entry point. @return Zero. */
int main(void) { return 0; }
