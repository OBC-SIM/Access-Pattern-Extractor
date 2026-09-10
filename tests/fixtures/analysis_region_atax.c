static double A[2][3], x[3], y[3], tmp[2];
static int outside;

/**
 * @brief Small fixed-size ATAX region using linked static objects.
 * @return Nothing; input preparation outside the region is excluded.
 */
void atax_region(void)
{
  outside = 1;
#pragma APE_ANALYZE_BEGIN
  for (int i = 0; i < 3; ++i) y[i] = 0;
  for (int i = 0; i < 2; ++i)
  {
    tmp[i] = 0;
    for (int j = 0; j < 3; ++j) tmp[i] += A[i][j] * x[j];
    for (int j = 0; j < 3; ++j) y[j] += A[i][j] * tmp[i];
  }
#pragma APE_ANALYZE_END
  outside = 2;
}

/**
 * @brief Supply a native ET_EXEC entry point for the same original source.
 * @return Zero; fixtures are analyzed without running the kernel.
 */
int main(void) { return 0; }
