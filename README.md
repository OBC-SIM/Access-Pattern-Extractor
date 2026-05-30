# Access-Pattern-Extractor

LLVM IR에서 루프·배열·스칼라 접근 패턴을 정적으로 추출하는 LLVM Pass 플러그인입니다.

[Yet-Another-Reuse-Distance-Analyzer](https://github.com/OBC-SIM/Yet-Another-Reuse-Distance-Analyzer)의 C++ 프론트엔드로 사용되며, `yard.analyze` / `yard.inline` 어노테이션 기반으로 분석 대상 함수를 필터링해 Loop Annotated Trace(LAT) JSON을 출력합니다.

---

## 출력 형식

`<name>_g_lat.json` — 함수별 LAT JSON.

| 노드 타입 | 주요 필드 | 설명 |
|-----------|-----------|------|
| Function wrapper | `function`, `params`, `annotations`, `body` | 함수 이름·파라미터·본문 |
| `Loop` | `var`, `start`, `bound`, `depth`, `body` | 중첩 루프 노드 |
| `Array` | `name`, `indices`, `op`, `shape`, `elem_size` | 배열 접근. `op`는 `"load"` 또는 `"store"` |
| `Scalar` | `name`, `op` | 스칼라 접근 |
| `Call` | `callee`, `args` | `yard.inline` 함수 호출 |

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
└── ArrayMetadata_test.cpp
```