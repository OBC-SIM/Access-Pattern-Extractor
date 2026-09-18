# Access-Pattern-Extractor

LLVM IR에서 루프·배열·스칼라 접근 패턴을 정적으로 추출하는 LLVM Pass 플러그인입니다.

[Yet-Another-Reuse-Distance-Analyzer](https://github.com/OBC-SIM/Yet-Another-Reuse-Distance-Analyzer)의 C++ 프론트엔드로 사용되며, `ape.analyze` / `ape.inline` 어노테이션 기반으로 분석 대상 함수를 필터링해 APE/MAP v2 JSON을 출력합니다.

함수 내부 구간을 선택하는 선택적 C++ 실행 파일 `yarda_region_map`도 제공합니다.
`APE_ANALYZE_BEGIN` / `APE_ANALYZE_END` 문법, 고정 Clang 14 파이프라인과 빌드·사용법은
[분석 구간 문서](docs/analysis-regions.md)를 참고하세요.

---

## 출력 형식

`<name>_g_ape.json` — APE/MAP v2 JSON.

> **Breaking change:** v2부터 JSON root는 function 배열이 아니라
> `schema_version`, `metadata`, `functions`를 갖는 object입니다.

```json
{
  "schema_version": 2,
  "metadata": {
    "objects": {
      "global::A": {
        "id": "global::A",
        "name": "A",
        "scope": "global",
        "storage": "global",
        "kind": "array",
        "shape": [100],
        "elem_type": "i32",
        "elem_size": 4,
        "llvm_type": "[100 x i32]"
      }
    },
    "structs": {}
  },
  "functions": [
    {
      "function": "test_constatnt_variable",
      "params": [],
      "annotations": ["ape.analyze"],
      "body": [
        {
          "type": "Loop",
          "var": "i",
          "start": 0,
          "bound": 50,
          "depth": 1,
          "body": [
            {
              "type": "Array",
              "name": "A",
              "object": "global::A",
              "indices": ["i"],
              "op": "store"
            }
          ]
        }
      ]
    }
  ]
}
```

### Root fields

| 필드 | 설명 |
|---|---|
| `schema_version` | APE/MAP schema version. 현재 값은 `2` |
| `metadata` | access node가 참조하는 object와 structure layout metadata |
| `functions` | 분석된 함수 wrapper 배열 |

### Metadata fields

`metadata.objects`는 access node의 `object` id로 참조됩니다.

| 필드 | 설명 |
|---|---|
| `id` | canonical object id. 예: `global::A`, `function:f::param:A` |
| `name` | 표시용 source-level 이름 |
| `scope` | `global` 또는 `function:<name>` |
| `storage` | `global`, `param`, `local` |
| `kind` | `array`, `pointer`, `struct`, `scalar` |
| `shape` | 배열 차원. 배열이 아니면 생략 가능 |
| `elem_type` | 파서가 사용할 원소 타입 이름 |
| `elem_size` | 원소 byte size |
| `llvm_type` | 디버깅용 LLVM IR type 문자열 |

`metadata.structs`는 구조체 ABI layout을 담습니다.

| 필드 | 설명 |
|---|---|
| `name` | 구조체 이름 |
| `size` | 구조체 byte size |
| `align` | 구조체 ABI alignment |
| `fields` | field metadata 배열 |

Field metadata는 `name`, `index`, `offset`, `size`와 함께 `kind`, `shape`,
`elem_type`, `elem_size`, `llvm_type`을 가질 수 있습니다. `-g` debug info에서
source type 이름을 확인할 수 있으면 `source_type`도 출력합니다.

`llvm_type`은 디버깅용입니다. 소비자는 LLVM type 문자열을 파싱하지 말고
`kind`, `shape`, `elem_type`, `elem_size`를 사용해야 합니다.

### Function body nodes

| 노드 타입 | 주요 필드 | 설명 |
|-----------|-----------|------|
| Function wrapper | `function`, `params`, `annotations`, `body` | 함수 이름·파라미터·본문 |
| `Loop` | `var`, `start`, `bound`, `depth`, `body` | 중첩 루프 노드 |
| `Array` | `name`, `object`, `indices`, `access_path`, `op` | 배열 접근. `object`가 있으면 shape/elem_size를 `metadata.objects`에서 조회 |
| `Scalar` | `name`, `object`, `op` | 식별 가능한 canonical storage ID를 보존하는 스칼라 접근 |
| `Call` | `callee`, `args`, `arg_objects` | `ape.inline` 함수 호출. `arg_objects`는 `args`와 같은 길이의 actual object/ref 문자열 배열 |

`Array.object`와 `Scalar.object`는 `global::<name>`,
`function:<name>::local:<name>`, `function:<name>::param:<name>` 형식이며,
ID가 `metadata.objects`에 등록된 경우에만 출력됩니다. 런타임 유도 pointer
base나 legacy access처럼 canonical storage를 정적으로 식별할 수 없으면
`object`를 생략합니다. `name`은 표시용이며 object lookup의 fallback이 아닙니다.

`Call.arg_objects`의 각 원소는 먼저 `metadata.objects` key로 해석합니다. key가
있으면 callee parameter access를 actual storage object로 치환할 수 있고,
없으면 loop induction variable이나 scalar value 같은 일반 ref로 취급합니다.

```json
{
  "type": "Call",
  "callee": "helper",
  "args": ["A", "i"],
  "arg_objects": ["global::A", "i"]
}
```

구조체와 배열이 섞인 access는 `access_path`로 field/index 순서를 보존합니다.
`name`은 사람이 읽기 위한 표시 문자열이고, 주소 계산 downstream은
`access_path`와 `metadata`를 사용해야 합니다.

```json
{
  "type": "Array",
  "name": "o.items[i].x",
  "object": "function:probe::param:o",
  "indices": ["i"],
  "access_path": [
    {"kind": "field", "name": "items", "index": 1},
    {"kind": "index", "value": "i"},
    {"kind": "field", "name": "x", "index": 0}
  ],
  "op": "store"
}
```

`tasks/test_struct.c`는 이 계약을 확인하는 fixture입니다. CTest의
`StructFixtureTrace`는 실제 `clang-14`와 `opt-14`를 실행해
`o.items[i].x`, `o.items[i].y`, `access_path`, `metadata.structs`의
field offset/type 정보가 유지되는지 검증합니다.

### Legacy 인덱스의 지원 범위

Legacy plugin은 인덱스를 `상수 + Σ(정수 계수 × 변수)`로 정확히 증명할 수
있을 때 변환합니다. 변수는 출력 `Loop`에 결속된 IV와 검증된 scalar formal입니다.
`2*i`, `i*2`, `2*i*2`, `2*(i*2)`, `i*(2*2)`, `2*i+1`, `7-i`, `8*i+j`를
지원하며, 비영 시작값·음수 step도 실제 출력 IV를 기준으로 계산합니다.
`ape.inline` 실제 인자에도 같은 판정을 적용합니다.
Unsigned formal은 zero extension과 inline 전달 관계에 맞춰 호출 인자도 같은
정수 의미로 기록합니다. 예를 들어 `unsigned char` 값 255는 `-1`로 치환하지
않습니다. 하나의 formal에 signed/unsigned cast가 충돌하거나 값 보존을 증명할
수 없는 narrowing이 필요하면 거부합니다.
Cast 증명은 정수 `trunc/sext/zext`를 대상으로 합니다. 포인터를 정수로 바꾸는
SCEV `ptrtoint`가 남은 인덱스는 `unsupported affine index` 진단으로 거부합니다.

Runtime 계수 `n*i`, 나눗셈·나머지와 비선형 식은 정확한 선형식으로 증명되지
않으면 `unsupported affine index` 진단으로 거부합니다. 계수·상수항을
버리거나 평탄화 식을 여러 배열 차원으로 바꾸지 않습니다. Struct GEP의 첫
피연산자는 필드 번호가 아니라 포인터 인덱스로 해석합니다.

출력은 괄호와 공백 없는 상수·식별자·`정수*식별자`의 합/차입니다. 식별자는
`[A-Za-z_][A-Za-z0-9_]*`이며, 항은 이름순으로 정렬하고 중복·0항을 정리합니다.
상수항은 마지막에 기록합니다. C의 `2*i*2`는 `4*i`, `7-i`는 `-i+7`로
출력합니다. `indices`와 `access_path`는 같은 식을 사용하며 `a[8*i+j]`는
인덱스 하나, `A[i][j]`는 두 개를 유지합니다. 계수·상수·최종 값은 signed int64
범위여야 합니다. Scalar formal 산술도 원래 폭에서 값 보존을 증명해야 하므로
모든 runtime 인자 수식이 지원되는 것은 아닙니다.

지원 Loop는 header의 정수 비교로 제어되며, 동일 PHI의 상수 start·bound·
0이 아닌 step과 정수 범위를 증명할 수 있어야 합니다. `bound`는 반복 횟수가
아닌 배타적 종료 값입니다. 알 수 없는 값에 0/1을 대신 넣거나 loop를 생략하지
않습니다. Narrowing과 zero extension은 값이 보존됨을 증명한 경우만 허용하고,
signed/unsigned 경계나 wrap을 증명할 수 없으면 거부합니다.

Debug 이름은 수식의 동치성을 증명하지 않습니다. 상수와 충돌하는 숫자 이름,
중첩 IV나 formal과 겹치는 이름은 정규화하며, debug 정보가 없는 scalar formal은 `argN`으로
표현합니다. 일반 임시 변수의 이름을 formal이나 IV의 이름으로 대신 사용하지 않습니다.
Inline 함수가 있는 모듈은 IV 이름을 함수별로 구분하고 모듈 내 formal 이름과의
충돌도 피합니다. 메모리 접근·inline 호출이 해당 IV의 Loop scope 밖에 있으면
미결속 IV 이름을 출력하지 않고 거부합니다.

변환 오류는 plugin의 nonzero 종료로 전달하며 MAP 출력 파일을 열기 전에
발생합니다. 새 MAP를 남기지 않고 기존 MAP 내용을 보존합니다. 과거에 잘못 생성된
MAP에는 원래 계수가 남아 있지 않으므로, 이 수정 이후 원본 C/IR에서 재생성해야 합니다.
MAP v2 JSON 구조를 유지하지만, 새 계수·다중 변수 식에는 H2-B C++ reader가
필요합니다. 구 reader는 문자열 trace에서 식을 그대로 반환하고 주소 분석에서
거부합니다. 구 reader는 schema 버전도 검사하지 않으므로 버전 번호만 올려
호환성을 보장할 수 없습니다. 새 입력에는 frontend/backend를 함께 갱신하세요.
R2 strict 구간의 scaled-index 거부 계약은 유지합니다.

`YardaIndexTests`와 `LegacyAffineRejection_*`는 region 옵션 OFF에서도 실행됩니다.
부모 저장소의 `yarda_affine_index_tests`는 같은 C의 debug/no-debug MAP와 ET_EXEC을
사용해 독립적인 byte offset·폭·operation·ordinal 및 두 캐시 oracle를 검증합니다.

---

## 빌드

**요구 사항:** LLVM 14, CMake ≥ 3.20, GCC ≥ 11, GTest

```bash
git clone https://github.com/OBC-SIM/Access-Pattern-Extractor
cd Access-Pattern-Extractor

cmake -S . -DLLVM_DIR=$(llvm-config-14 --cmakedir) -B build
cmake --build build
```

빌드 산출물:
- `build/libMemoryAccessPatterns.so` — opt에 로드할 Pass 플러그인
- `build/MemoryAccessPatternsTests` — GTest 바이너리

---

## 사용법

간단한 실행은 repo root의 `main.py`를 사용합니다.

```bash
python3 main.py tasks/test_call.c
```

`.c` 입력은 `clang-14`로 `<name>_g.ll`을 만든 뒤 pass를 실행하고,
현재 디렉토리에 `<name>_g_ape.json`을 생성합니다. `.ll` 입력은 컴파일 없이
바로 pass에 전달합니다.

CASA 저장소의 `frontend/main.py` wrapper는 C/LL 입력에서 APE JSON을 만든 뒤
바로 cache simulation report까지 생성할 수 있습니다.

```bash
python3 frontend/main.py frontend/tasks/test_call.c \
  --cache settings/cache.yaml --output results --no-color
```

APE JSON까지만 생성하려면 `--ape-only`를 사용합니다.

```bash
python3 frontend/main.py frontend/tasks/test_call.c --ape-only
```

수동으로 실행하려면 다음과 같이 `clang-14`와 `opt-14`를 호출합니다.

```bash
# 1. C 소스 → LLVM IR
clang-14 -O0 -Xclang -disable-O0-optnone -g \
         -emit-llvm -S -o <name>_g.ll <name>.c

# 2. Pass 실행 → APE JSON 생성
opt-14 -load-pass-plugin ./build/libMemoryAccessPatterns.so \
       -passes=function\(mem2reg\),loop-simplify,loop-annotated-trace \
       <name>_g.ll -o /dev/null
```

현재 디렉토리에 `<name>_g_ape.json`이 생성됩니다.

### 어노테이션

```c
#include "ape_analyze.h"
```

- `APE_ANALYZE` — 분석 root 함수. 어노테이션이 없으면 모든 함수를 분석합니다.
- `APE_INLINE` — call site에 MAP 노드로 보존할 helper 함수.
- `yard_analyze.h`의 `YARD_ANALYZE` / `YARD_INLINE`은 기존 fixture 호환을
  위해 같은 annotation으로 유지됩니다.

캐시/주소 계산 downstream에서는 실제 storage object의 full shape가 필요합니다.
C 함수 파라미터의 배열 표기는 LLVM IR에서 pointer-to-row 형태로 decay되어
outer dimension이 사라질 수 있으므로, fixture와 benchmark는 다음 구조를
권장합니다.

```c
float A[32][64], B[64][32], C[32][32];

APE_INLINE
void matmul_params(float A[32][64], float B[64][32], float C[32][32])
{
  /* loop body */
}

APE_ANALYZE
void matmul_params_kernel(void)
{
  matmul_params(A, B, C);
}
```

즉 `APE_ANALYZE` root는 실제 global/local object를 call site에서 드러내고,
계산 본문은 `APE_INLINE` helper로 둡니다. 이때 `Call.arg_objects`에는
`global::A` 같은 actual object id가 기록되어 downstream이 callee parameter를
실제 object metadata에 binding할 수 있습니다.

---

## 테스트

```bash
ctest --test-dir build
# 또는
./build/MemoryAccessPatternsTests
```

| 테스트 스위트 | 내용 |
|---|---|
| `ScalarAccess` | 스칼라 접근 생성 및 JSON 직렬화 |
| `ArrayAccess` | 배열 접근 생성·op 필드·상수 인덱스 |
| `LoopNest` | 루프 트리 구성 및 중첩 JSON 직렬화 |
| `CallStmt` | 함수 호출 노드 생성 및 `arg_objects` JSON 직렬화 |
| `AccessBuilder` | load/store/call statement 생성 및 call argument object 추출 |
| `AccessMetadataBuilder` | `metadata.objects`, `metadata.structs` 생성 |
| `AccessPath` | 구조체 field와 배열 index 순서 보존 |
| `GetBaseName` | 무명 변수 IR 슬롯 번호 구분 |
| `GetValueName` | 함수 파라미터·call argument 이름 추출 |
| `GetIndexVars` | 전역/파라미터 배열 GEP chain 다차원 index 보존 |
| `FunctionAnnotation` | `ape.analyze` / `ape.inline` annotation 감지 |
| `StructFixtureTrace` | 구조체 fixture의 실제 pass 출력 검증 |

---

## 프로젝트 구조

```
main.py                     # C/LLVM IR → _ape.json pipeline entrypoint
include/
├── AccessBuilder.hpp        # 명령어 → Statement 변환 인터페이스
├── AccessMetadata.hpp       # object / struct metadata model
├── AccessMetadataBuilder.hpp # module metadata 생성 인터페이스
├── AccessPath.hpp           # GEP → structured access_path 변환
├── AccessTypeInfo.hpp       # LLVM/source type → access type 정보
├── ArrayMetadata.hpp        # 배열 shape / element size metadata
├── Statement.hpp            # AST 노드
├── JsonExportVisitor.hpp    # Visitor: AST → llvm::json::Value
└── IrHelpers.hpp            # IR 쿼리 헬퍼 선언
src/
├── AccessBuilder.cpp        # Load/Store/Call → Statement 변환
├── AccessMetadata.cpp       # metadata JSON 직렬화
├── AccessMetadataBuilder.cpp # globals/functions/struct metadata 수집
├── AccessPath.cpp           # field/index 순서 보존 access path 생성
├── AccessTypeInfo.cpp       # type 정보 추출 구현
├── ArrayMetadata.cpp        # GEP source type → ArrayMetadata 추출
├── IrHelpers.cpp            # buildDebugNameMap, getIndexVars 등
└── LoopAnalysisPass.cpp     # LLVM Pass 진입점: _ape.json 출력
tests/
├── AccessBuilder_test.cpp
├── AccessMetadataBuilder_test.cpp
├── AccessPath_test.cpp
├── CallStmt_test.cpp
├── Statement_test.cpp
├── IrHelpers_test.cpp
├── IrHelpersGep_test.cpp
├── ArrayMetadata_test.cpp
└── run_struct_fixture.sh
```
