#include "yard_analyze.h"

// 포인터 간접 참조의 canonical storage object 계약을 확인하는 픽스처.
//
// baseObject()는 GEP 체인을 벗겨낸 base가 load일 때 그 load의 대상이
// alloca인 경우에만 storage로 인정한다. 나머지는 canonical storage를
// 정적으로 정할 수 없으므로 object를 생략해야 한다. 아래 unresolved_*
// 함수가 그 경로이고, resolved_* 함수가 대조군이다.

#define N 64

int storage[N];
int other[N];

int * global_ptr;

struct Holder
{
  int   pad;
  int * data;
};

struct Holder holder;

// 전역 포인터 변수를 통한 접근. base는 @global_ptr에서 읽은 값이라
// alloca가 아니므로 object를 생략한다.
YARD_ANALYZE
void unresolved_global_pointer(void)
{
  for (int i = 0; i < N; i++) global_ptr[i] = i;
}

// struct 필드에 저장된 포인터를 통한 접근. base는 필드 GEP에서 읽은
// 값이므로 위와 같은 경로를 탄다.
YARD_ANALYZE
void unresolved_struct_field_pointer(void)
{
  for (int i = 0; i < N; i++) holder.data[i] = i;
}

// 제어 흐름으로 갈리는 포인터. debug 정보 덕분에 name은 "p[i]"로 정상
// 해석되지만 object는 생략한다. 이름만 보면 정상 접근과 구분되지 않는다.
YARD_ANALYZE
void unresolved_selected_pointer(int flag)
{
  int * p = flag ? storage : other;
  for (int i = 0; i < N; i++) p[i] = i;
}

// 대조군: mem2reg로 승격되는 지역 포인터. 전역 배열로 접히므로 해석된다.
YARD_ANALYZE
void resolved_local_pointer(void)
{
  int * p = storage;
  for (int i = 0; i < N; i++) p[i] = i;
}

// 대조군: 일반 포인터 파라미터. base가 Argument라 param object로 해석된다.
YARD_INLINE
void resolved_pointer_param_kernel(int * p, int i) { p[i] = i; }

YARD_ANALYZE
void resolved_pointer_param(void)
{
  for (int i = 0; i < N; i++) resolved_pointer_param_kernel(storage, i);
}

// 대조군: 전역 배열 직접 접근.
YARD_ANALYZE
void resolved_direct_global(void)
{
  for (int i = 0; i < N; i++) storage[i] = i;
}
