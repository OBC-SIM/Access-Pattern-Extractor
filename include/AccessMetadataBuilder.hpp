#pragma once

#include <utility>
#include <string>
#include <vector>

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Value.h"

#include "AccessMetadata.hpp"
#include "IrHelpers.hpp"

namespace lat {

/**
 * @brief 모듈의 LLVM IR/debug info에서 LAT root metadata를 수집한다.
 *
 * 구조체 layout은 ABI 기준 byte offset/size를 사용한다. Debug info가 있으면
 * source-level field name을 사용하고, 없으면 field_N으로 fallback한다.
 *
 * @param M metadata를 수집할 LLVM module
 * @return LAT v2 root metadata
 */
AccessMetadata buildAccessMetadata(llvm::Module& M);

/**
 * @brief 포인터 값이 가리키는 base storage object의 canonical id를 만든다.
 *
 * Global, function argument, local alloca를 구분해 같은 display name 충돌을
 * 피한다. 알 수 없는 임시 값은 현재 함수 scope의 temp object로 표시한다.
 *
 * @param Ptr     load/store 또는 GEP pointer operand
 * @param Current 현재 분석 중인 함수
 * @param names   debug name map
 * @return LAT metadata `objects` 키로 사용할 object id
 */
std::string getObjectId(llvm::Value* Ptr, const llvm::Function& Current,
                        const NameMap& names);

/**
 * @brief GEP 접근을 구조체 필드명과 배열 인덱스로 나눈 표시 정보를 만든다.
 *
 * 반환되는 name은 `o.items.x`처럼 얕은 display name이며, indices에는 배열
 * 차원의 인덱스만 들어간다. 구조체 field index는 metadata로 해석한다.
 *
 * @param GEP      분석할 GEP
 * @param SE       ScalarEvolution 분석 결과
 * @param names    debug name map
 * @param metadata root metadata
 * @return display name과 배열 index 목록
 */
std::pair<std::string, std::vector<std::string>> describeGepAccess(
    llvm::GEPOperator* GEP,
    llvm::ScalarEvolution& SE,
    const NameMap& names,
    const AccessMetadata& metadata);

}  // namespace lat
