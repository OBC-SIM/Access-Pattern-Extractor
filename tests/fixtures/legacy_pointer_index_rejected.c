int a[64];

void first(void) { a[0] = 1; }

#ifndef H2_INDEX
#error "H2_INDEX must select the pointer-derived expression under test"
#endif

void kernel(int * p)
{
  for (int i = 0; i < 4; ++i) a[H2_INDEX] = 1;
}
