#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "ArrayMetadata.hpp"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"

namespace map
{

/// Value* → 원본 소스 변수명 맵 (llvm.dbg.value intrinsic 기반)
using NameMap = std::unordered_map<const llvm::Value *, std::string>;

/**
 * @brief 함수 내 llvm.dbg.value intrinsic을 스캔해 Value → 변수명 맵을
 * 구성한다.
 *
 * -g 없이 컴파일된 IR에서는 빈 맵을 반환하며, 호출자는 fallback을 처리해야
 * 한다.
 *
 * @param F  분석 대상 함수
 * @return   Value* → 변수명 맵
 */
NameMap buildDebugNameMap(llvm::Function & F);

/**
 * @brief 출력 Loop를 제어하는 PHI의 충돌 없는 이름을 반환한다.
 *
 * Debug 이름은 PHI 선택 이후에 사용한다. 숫자 상수나 바깥 IV와 충돌하는
 * 이름은 정규화한다.
 *
 * @param L      대상 루프
 * @param SE     ScalarEvolution 분석 결과
 * @param names  buildDebugNameMap 결과
 * @return 인덱스 해석과 공유하는 결정적 변수 이름.
 * @throws std::invalid_argument 제어 PHI를 확인할 수 없는 경우.
 */
std::string getInductionVarName(llvm::Loop * L, llvm::ScalarEvolution & SE,
                                const NameMap & names);

/**
 * @brief GEP 인덱스 하나를 손실 없이 MAP 인덱스 하나로 변환한다.
 *
 * 정수 상수(0 포함), 결속된 IV와 검증된 scalar formal의 선형식을 보존한다.
 * 계수와 복합 항을 버리거나 하나의 식을 여러 차원으로 분리하지 않는다.
 *
 * @param Idx    GEP 인덱스 피연산자
 * @param SE     ScalarEvolution 분석 결과
 * @param names  buildDebugNameMap 결과
 * @return 항상 원소가 하나인 벡터. 기존 API 형태를 유지한다.
 * @throws std::invalid_argument 의미·범위·결속을 증명하지 못한 경우.
 */
std::vector<std::string> resolveIndex(llvm::Value * Idx,
                                      llvm::ScalarEvolution & SE,
                                      const NameMap & names);

/**
 * @brief GEP 연산의 각 인덱스를 손실 없는 affine 식으로 변환한다.
 *
 * GetElementPtrInst(명령어 GEP)와 ConstantExpr GEP(전역 배열 상수 접근)를
 * 모두 처리하기 위해 GEPOperator를 인자로 받는다.
 * multi-index GEP에서 첫 번째 인덱스가 상수 0이면 포인터 역참조로 보고
 * 건너뛴다.
 *
 * @param GEP    분석할 GEPOperator (GetElementPtrInst 또는 ConstantExpr GEP)
 * @param SE     ScalarEvolution 분석 결과
 * @param names  buildDebugNameMap 결과
 * @param useSite 실제 접근 위치. nullptr이면 최상위 GEP 위치를 사용한다.
 * @return GEP chain의 각 인덱스에 대응하는 순서 있는 값 목록.
 * @throws std::invalid_argument 하나라도 손실 없이 해석할 수 없는 경우.
 */
std::vector<std::string>
getIndexVars(llvm::GEPOperator * GEP, llvm::ScalarEvolution & SE,
             const NameMap & names,
             const llvm::Instruction * useSite = nullptr);

/**
 * @brief 포인터 피연산자에서 배열/변수의 기반 이름을 추출한다.
 *
 * NameMap → IR 이름 → "argN" → IR 슬롯 번호 순으로 fallback한다.
 * 무명 값도 슬롯 번호(e.g. "3")로 유일하게 식별된다.
 *
 * @param Ptr    GEP의 포인터 피연산자
 * @param names  buildDebugNameMap 결과
 */
std::string getBaseName(llvm::Value * Ptr, const NameMap & names);

/**
 * @brief 일반 LLVM Value를 MAP JSON에 기록할 이름으로 변환한다.
 *
 * 포인터 값은 배열/스칼라 base 이름으로, 정수 상수는 숫자 문자열로,
 * 그 외 값은 debug name → IR name → IR 슬롯 번호 순으로 변환한다.
 * 무명 인자는 상수와 충돌하지 않는 argN 이름을 사용한다.
 *
 * @param V      변환할 LLVM 값
 * @param names  buildDebugNameMap 결과
 */
std::string getValueName(llvm::Value * V, const NameMap & names);

/**
 * @brief 함수가 clang annotate attribute로 지정된 annotation을 갖는지 확인한다.
 *
 * Clang은 `__attribute__((annotate("...")))` 정보를
 * `@llvm.global.annotations` 전역에 기록한다. 이 helper는 해당 전역을
 * 해석해 지정된 함수와 annotation 문자열의 매칭 여부를 반환한다.
 *
 * @param F          검사할 함수
 * @param Annotation 찾을 annotation 문자열
 * @return annotation이 존재하면 true
 */
bool hasFunctionAnnotation(llvm::Function & F, llvm::StringRef Annotation);

/**
 * @brief GEP source type에서 배열 shape와 element byte size를 추출한다.
 *
 * @param GEP 분석할 GEPOperator
 * @param DL  모듈 DataLayout
 * @return 추론 가능한 배열 metadata. shape를 모르면 비워두고 elem_size만 반환할
 * 수 있다.
 */
ArrayMetadata getArrayMetadata(llvm::GEPOperator * GEP,
                               const llvm::DataLayout & DL);

}  // namespace map
