#include "yard_analyze.h"

struct S {
  int x;
  double y;
};

struct Outer {
  int tag;
  struct S items[4];
};

YARD_ANALYZE
void struct_field_access(struct Outer *o)
{
  for (int i = 0; i < 4; i++) {
    o->items[i].x = i;
    o->items[i].y = (double)i;
  }
}
