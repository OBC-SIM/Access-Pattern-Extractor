int a[64];

void first(void) { a[0] = 1; }

#ifndef H2_INDEX
#error "H2_INDEX must select the expression under test"
#endif
#ifndef H2_START
#define H2_START 0
#endif
#ifndef H2_STEP
#define H2_STEP 1
#endif

void kernel(int n)
{
  for (int i = H2_START; i < 8; i += H2_STEP)
  {
    int t = 2 * i;
    for (int j = 0; j < 3; ++j) a[H2_INDEX] = 1;
  }
}
