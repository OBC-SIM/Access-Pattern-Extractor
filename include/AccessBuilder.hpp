#pragma once

#include <memory>
#include <set>

#include "IrHelpers.hpp"
#include "Statement.hpp"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

namespace map
{

struct AccessMetadata;

/**
 * @brief Preserve positional arguments and canonical objects for a direct call.
 * @param call Borrowed instruction; no IR is modified.
 * @param names Borrowed source names for argument and object binding.
 * @param current Borrowed caller owning local storage identities.
 * @return Owned Call statement, or nullptr for an indirect call.
 */
std::unique_ptr<Statement> makeDirectCall(llvm::CallBase & call,
                                          const NameMap & names,
                                          const llvm::Function & current);

/**
 * @brief LLVM instruction 하나를 MAP access/call Statement로 변환한다.
 *
 * Load/Store 접근은 op 필드에 각각 "load"/"store" 계약을 보존한다.
 * `ape.inline` 함수로의 direct call은 CallStmt로 보존하고, 그 외
 * 명령은 분석 대상이 아니면 nullptr을 반환한다.
 *
 * @param I           변환할 LLVM instruction
 * @param SE          ScalarEvolution 분석 결과
 * @param names       LLVM value를 source-level 이름으로 복원하는 맵
 * @param inlineFuncs `ape.inline` annotation이 붙은 함수 집합
 * @param current     현재 분석 중인 함수
 * @return 변환된 Statement. 분석 대상이 아니면 nullptr
 * @throws std::invalid_argument 인덱스나 포인터 PHI의 affine 이동량을 증명하지
 * 못하거나 배열의 원소 배율과 접근 폭이 호환되지 않는 경우. 식별 가능한
 * 저장 객체의 GEP chain에 pointer cast가 있는 경우에도 거부한다.
 * 이 접근을 조용히 생략하지 않는다.
 * @note Runtime 주소는 object ID 없는 노드로 보존될 수 있다. 이는 실제
 * 주소나 상대 offset의 증명이 아니며 ELF 소비자는 이 노드를 거부한다.
 */
std::unique_ptr<Statement>
makeAccessFromInstr(llvm::Instruction & I, llvm::ScalarEvolution & SE,
                    const NameMap & names, const AccessMetadata & metadata,
                    const std::set<const llvm::Function *> & inlineFuncs,
                    const llvm::Function & current);

}  // namespace map
