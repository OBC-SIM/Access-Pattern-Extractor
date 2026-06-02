# Access-Pattern-Extractor

LLVM IR에서 루프·배열·스칼라 접근 패턴을 정적으로 추출하는 LLVM Pass 플러그인입니다.

[Yet-Another-Reuse-Distance-Analyzer](https://github.com/OBC-SIM/Yet-Another-Reuse-Distance-Analyzer)의 C++ 프론트엔드로 사용되며, `yard.analyze` / `yard.inline` 어노테이션 기반으로 분석 대상 함수를 필터링해 Loop Annotated Trace(LAT) JSON을 출력합니다.

---

## 출력 형식

`<name>_g_lat.json` — LAT v2 JSON.

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
      "annotations": ["yard.analyze"],
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
              "shape": [100],
              "elem_size": 4,
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
| `schema_version` | LAT schema version. 현재 값은 `2` |
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
| `Array` | `name`, `object`, `indices`, `op`, `shape`, `elem_size` | 배열 접근. `object`는 `metadata.objects` key |
| `Scalar` | `name`, `op` | 스칼라 접근 |
| `Call` | `callee`, `args`, `arg_objects` | `yard.inline` 함수 호출. `arg_objects`는 `args`와 같은 길이의 actual object/ref 문자열 배열 |

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

구조체 access는 깊은 path 배열 대신 얕은 표시 이름을 사용합니다.

```json
{
  "type": "Array",
  "name": "o.items.x",
  "object": "function:probe::param:o",
  "indices": ["i"],
  "op": "store"
}
```

`tasks/test_struct.c`는 이 계약을 확인하는 fixture입니다. CTest의
`StructFixtureTrace`는 실제 `clang-14`와 `opt-14`를 실행해 `o.items.x`,
`o.items.y`, `metadata.structs`의 field offset/type 정보가 유지되는지
검증합니다.

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
- `build/libLoopAnnotatedTrace.so` — opt에 로드할 Pass 플러그인
- `build/LoopAnnotatedTraceTests` — GTest 바이너리

---

## 사용법

```bash
# 1. C 소스 → LLVM IR
clang-14 -O0 -Xclang -disable-O0-optnone -g \
         -emit-llvm -S -o <name>_g.ll <name>.c

# 2. Pass 실행 → LAT JSON 생성
opt-14 -load-pass-plugin ./build/libLoopAnnotatedTrace.so \
       -passes=function\(mem2reg\),loop-simplify,loop-annotated-trace \
       <name>_g.ll -o /dev/null
```

현재 디렉토리에 `<name>_g_lat.json`이 생성됩니다.

### 어노테이션

```c
#define YARD_ANALYZE __attribute__((annotate("yard.analyze")))
#define YARD_INLINE  __attribute__((annotate("yard.inline")))
```

- `YARD_ANALYZE` — 분석 root 함수. 어노테이션이 없으면 모든 함수를 분석합니다.
- `YARD_INLINE` — call site에 LAT 노드로 보존할 helper 함수.

---

## 테스트

```bash
ctest --test-dir build
# 또는
./build/LoopAnnotatedTraceTests
```

| 테스트 스위트 | 내용 |
|---|---|
| `ScalarAccess` | 스칼라 접근 생성 및 JSON 직렬화 |
| `ArrayAccess` | 배열 접근 생성·op 필드·상수 인덱스 |
| `LoopNest` | 루프 트리 구성 및 중첩 JSON 직렬화 |
| `CallStmt` | 함수 호출 노드 생성 및 JSON 직렬화 |
| `GetBaseName` | 무명 변수 IR 슬롯 번호 구분 |
| `GetValueName` | 함수 파라미터·call argument 이름 추출 |
| `GetIndexVars` | 전역/파라미터 배열 GEP chain 다차원 index 보존 |
| `FunctionAnnotation` | `yard.analyze` / `yard.inline` annotation 감지 |
| `StructFixtureTrace` | 구조체 fixture의 실제 pass 출력 검증 |

---

## 프로젝트 구조

```
include/
├── AccessBuilder.hpp     # 명령어 → Statement 변환 인터페이스
├── ArrayMetadata.hpp     # 배열 shape / element size metadata
├── Statement.hpp         # AST 노드 (ScalarAccess, ArrayAccess, LoopNest, CallStmt)
├── JsonExportVisitor.hpp # Visitor: AST → llvm::json::Value
└── IrHelpers.hpp         # IR 쿼리 헬퍼 선언
src/
├── AccessBuilder.cpp     # Load/Store/Call → Statement 변환 (op 필드 포함)
├── ArrayMetadata.cpp     # GEP source type → ArrayMetadata 추출
├── IrHelpers.cpp         # buildDebugNameMap, getIndexVars, getBaseName 등
└── LoopAnalysisPass.cpp  # LLVM Pass 진입점: buildRootStatements → LAT JSON 출력
tests/
├── Statement_test.cpp
├── IrHelpers_test.cpp
├── IrHelpersGep_test.cpp
├── ArrayMetadata_test.cpp
└── run_struct_fixture.sh
```
