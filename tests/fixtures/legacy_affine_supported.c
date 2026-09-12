#define ANALYZE __attribute__((annotate("ape.analyze")))
#define INLINE __attribute__((annotate("ape.inline")))

int a[16];
int matrix[2][3];
struct Record
{
  char padding;
  int value;
} records[4];
_Static_assert(sizeof(struct Record) == 8,
               "fixture requires eight-byte records");

ANALYZE void offset(void)
{
  for (int i = 0; i < 3; ++i) a[i + 1] = 1;
}

ANALYZE void stride(void)
{
  for (int i = 2; i < 8; i += 2) a[i] = 1;
}

ANALYZE void descending(void)
{
  for (int i = 5; i > 0; --i) a[i - 1] = 1;
}

ANALYZE void constant(void)
{
  for (int i = 0; i < 3; ++i) a[0 * i + 3] = 1;
}

ANALYZE void dimensions(void)
{
  for (int i = 0; i < 2; ++i)
    for (int j = 0; j < 3; ++j) matrix[i][j] = 1;
}

INLINE void touch(int index) { a[index] = a[0] + 1; }

ANALYZE void caller(void)
{
  for (int i = 1; i < 4; ++i) touch(i);
}

ANALYZE void fields(void)
{
  struct Record * p = records;
  for (int i = 0; i < 3; ++i) p[i + 1].value = 1;
}

int main(void) { return 0; }

INLINE void scoped(int i)
{
  a[i] = 1;
  for (int i = 0; i < 2; ++i) a[i] = 1;
}

ANALYZE void shadowed(void) { scoped(5); }

INLINE void captured(int x)
{
  for (int i = 0; i < 2; ++i) a[x] = 1;
}

ANALYZE void capture(void)
{
  for (int i = 0; i < 3; ++i) captured(i);
}

INLINE void touch_unsigned(unsigned index) { a[index] = 1; }

ANALYZE void unsigned_caller(void)
{
  for (int i = 1; i < 4; ++i) touch_unsigned(i);
}

INLINE void forward_unsigned(unsigned index) { touch_unsigned(index); }

ANALYZE void unsigned_forward(void)
{
  for (int i = 1; i < 4; ++i) forward_unsigned(i);
}

int unsigned_matrix[2][256];

INLINE void touch_byte(unsigned char index) { unsigned_matrix[1][index] = 1; }

ANALYZE void unsigned_boundary(void)
{
  touch_byte(127);
  touch_byte(128);
  touch_byte(255);
}
