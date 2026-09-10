#pragma once

#include <memory>
#include <set>

#include "IrHelpers.hpp"
#include "Statement.hpp"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

namespace lat
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
 * @brief LLVM instruction 하나를 LAT access/call Statement로 변환한다.
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
 */
std::unique_ptr<Statement>
makeAccessFromInstr(llvm::Instruction & I, llvm::ScalarEvolution & SE,
                    const NameMap & names, const AccessMetadata & metadata,
                    const std::set<const llvm::Function *> & inlineFuncs,
                    const llvm::Function & current);

}  // namespace lat
