#include <stdlib.h>

#define ANALYZE __attribute__((annotate("ape.analyze")))
#define INLINE __attribute__((annotate("ape.inline")))

unsigned char bytes[32] __attribute__((aligned(16)));
unsigned int rows[2][3];
struct Record
{
  unsigned char tag;
  unsigned int value;
};
struct Record record;

_Static_assert(sizeof(unsigned int) == 4, "fixture requires four-byte words");
_Static_assert(__builtin_offsetof(struct Record, value) == 4,
               "fixture requires the target's four-byte field alignment");

INLINE void wide_scalar_helper(unsigned char * p)
{
  *(unsigned int *)p = 0x12345678U;
}

INLINE void mixed_width_helper(unsigned char * p)
{
  *p = 1;
  *(unsigned int *)p = 2;
  p[1] = *p;
}

INLINE void matching_row_helper(unsigned int p[][3]) { p[1][1] = 1; }
INLINE void field_helper(struct Record * p) { p->value = 1; }

ANALYZE void scalar_cast_width(void) { wide_scalar_helper(bytes); }
ANALYZE void mixed_access_widths(void) { mixed_width_helper(bytes); }
ANALYZE void matching_row_layout(void) { matching_row_helper(rows); }
ANALYZE void matching_field_layout(void) { field_helper(&record); }

INLINE void int_gep_helper(unsigned int * p) { p[1] = 1; }
INLINE void int_pointer_helper(unsigned int * p)
{
  for (int i = 0; i < 3; ++i) *p++ = 1;
}
INLINE void mismatching_row_helper(unsigned int p[][2]) { p[1][1] = 1; }

ANALYZE void incompatible_gep_width(void)
{
  int_gep_helper((unsigned int *)bytes);
}

ANALYZE void incompatible_pointer_width(void)
{
  int_pointer_helper((unsigned int *)bytes);
}

ANALYZE void incompatible_row_layout(void)
{
  mismatching_row_helper((unsigned int(*)[2])rows);
}

ANALYZE void runtime_cast_before_gep(void)
{
  unsigned char * buf = malloc(16);
  ((unsigned int *)buf)[1] = 1;
  free(buf);
}

ANALYZE void runtime_cast_after_gep(void)
{
  unsigned char * buf = malloc(16);
  *(unsigned int *)(buf + 4) = 1;
  free(buf);
}

ANALYZE void runtime_pointer_induction(void)
{
  unsigned int * buf = malloc(16);
  unsigned int * p = buf;
  for (int i = 0; i < 3; ++i) *p++ = 1;
  free(buf);
}

ANALYZE void runtime_indexed_induction(void)
{
  unsigned int * p = malloc(16);
  for (int i = 0; i < 3; ++i) p[i] = 1;
  free(p);
}

int main(void) { return 0; }
