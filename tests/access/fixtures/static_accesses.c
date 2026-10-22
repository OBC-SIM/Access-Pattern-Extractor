#define ANALYZE __attribute__((annotate("ape.analyze")))
#define INLINE __attribute__((annotate("ape.inline")))

int storage[32];
int scalar;
int matrix[2][3];

ANALYZE void pointer_forward(void)
{
  int * p = storage;
  for (int i = 0; i < 4; ++i) *p++ = 1;
}

ANALYZE void pointer_reverse(void)
{
  int * p = storage + 6;
  for (int i = 0; i < 3; ++i)
  {
    *p = 1;
    p -= 2;
  }
}

ANALYZE void pointer_offset(void)
{
  int * p = storage + 1;
  for (int i = 0; i < 3; ++i, ++p) p[1] = 1;
}

ANALYZE void pointer_nonunit(void)
{
  int * p = storage;
  for (int i = 0; i < 6; i += 2) *p++ = 1;
}

ANALYZE void mixed_inductions(void)
{
  for (int i = 0, j = 0; i < 6; i += 2, ++j)
  {
    storage[i] = 1;
    storage[j] = 2;
  }
}

ANALYZE void descending_inductions(void)
{
  for (int i = 7, j = 2; i > 0; i -= 3, ++j)
  {
    storage[i] = 1;
    storage[j] = 2;
  }
}

ANALYZE void nested_inductions(void)
{
  for (int i = 2, j = 0; i < 6; i += 2, ++j)
    for (int k = 0; k < i; ++k) storage[8 * j + k] = 1;
}

ANALYZE void not_equal_forward(void)
{
  for (int i = 0; i != 6; i += 2) storage[i] = 1;
}

ANALYZE void not_equal_reverse(void)
{
  for (int i = 6; i != 0; i -= 2) storage[i] = 1;
}

INLINE void scalar_helper(int * p) { *p = 1; }
INLINE void scalar_forwarder(int * p) { scalar_helper(p); }

ANALYZE void scalar_actual(void) { scalar_forwarder(&scalar); }

INLINE void pointer_helper(int * p)
{
  for (int i = 0; i < 3; ++i) *p++ = 1;
}

ANALYZE void pointer_actual(void) { pointer_helper(storage); }

ANALYZE void pointer_row(void)
{
  int * p = matrix[1];
  for (int i = 0; i < 3; ++i) *p++ = 1;
}

ANALYZE void pointer_nested_carry(void)
{
  int * p = storage + 1;
  for (int i = 0; i < 2; ++i)
    for (int j = 0; j < 3; ++j) *p++ = 1;
}

ANALYZE void pointer_nested_reverse(void)
{
  int * p = storage + 7;
  for (int i = 0; i < 2; ++i)
    for (int j = 0; j < 3; ++j) *p-- = 1;
}

int main(void) { return 0; }
