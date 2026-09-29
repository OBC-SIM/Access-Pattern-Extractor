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

ANALYZE void scaled_left(void)
{
  for (int i = 0; i < 4; ++i) a[2 * i] = 1;
}

ANALYZE void scaled_right(void)
{
  for (int i = 0; i < 4; ++i) a[i * 2] = 1;
}

ANALYZE void chained(void)
{
  for (int i = 0; i < 4; ++i) a[2 * i * 2] = 1;
}

ANALYZE void nested_product(void)
{
  for (int i = 0; i < 4; ++i) a[2 * (i * 2)] = 1;
}

ANALYZE void constant_product(void)
{
  for (int i = 0; i < 4; ++i) a[i * (2 * 2)] = 1;
}

ANALYZE void temporary(void)
{
  for (int i = 0; i < 4; ++i)
  {
    int t = 2 * i;
    a[t] = 1;
  }
}

ANALYZE void scaled_stride(void)
{
  for (int i = 2; i < 8; i += 2) a[2 * i + 1] = 1;
}

ANALYZE void scaled_descending(void)
{
  for (int i = 5; i > 0; i -= 2) a[2 * i + 1] = 1;
}

ANALYZE void negative_coefficient(void)
{
  for (int i = 0; i < 4; ++i) a[7 - i] = 1;
}

ANALYZE void flattened(void)
{
  for (int i = 0; i < 2; ++i)
    for (int j = 0; j < 3; ++j) a[8 * i + j] = 1;
}

ANALYZE void scaled_fields(void)
{
  struct Record * p = records;
  for (int i = 0; i < 2; ++i) p[2 * i].value = 1;
}

int affine_data[1024];

INLINE void affine_byte(unsigned char x) { affine_data[2 * (int)x + 1] = 1; }
INLINE void affine_forward(unsigned char x) { affine_byte(x); }

ANALYZE void affine_actual(void)
{
  for (int i = 0; i < 3; ++i) affine_forward(i + 1);
}

ANALYZE void affine_boundary(void)
{
  affine_byte(127);
  affine_byte(128);
  affine_byte(255);
}

int triangle_matrix[4][4];

ANALYZE void triangle(void)
{
  for (int i = 0; i < 4; ++i)
    for (int j = i; j < 4; ++j) triangle_matrix[i][j] = 1;
}

ANALYZE void triangle_strict(void)
{
  for (int i = 0; i < 4; ++i)
    for (int j = i + 1; j < 4; ++j) triangle_matrix[i][j] = 1;
}

ANALYZE void triangle_descending(void)
{
  for (int i = 0; i < 4; ++i)
    for (int j = i; j >= 0; j -= 2) triangle_matrix[i][j] = 1;
}

ANALYZE void triangle_scaled(void)
{
  for (int i = 0; i < 2; ++i)
    for (int j = 2 * i + 1; j < 4; j += 2) triangle_matrix[i][j] = 1;
}

ANALYZE void triangle_nested(void)
{
  for (int i = 0; i < 4; ++i)
    for (int j = i; j < 4; ++j)
      for (int k = j + 1; k < 4; ++k) triangle_matrix[j][k] = 1;
}

ANALYZE void triangle_syrk(void)
{
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j <= i; ++j) triangle_matrix[i][j] = 1;
}

ANALYZE void triangle_nussinov(void)
{
  for (int i = 3; i >= 0; --i)
    for (int j = i + 1; j < 4; ++j)
      for (int k = i + 1; k < j; ++k) triangle_matrix[i][k] = 1;
}

ANALYZE void triangle_end_descending(void)
{
  for (int i = 0; i < 4; ++i)
    for (int j = 3; j >= i; j -= 2) triangle_matrix[i][j] = 1;
}
