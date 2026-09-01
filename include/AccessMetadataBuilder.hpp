#pragma once

#include <string>

#include "llvm/IR/Module.h"
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
 * 피한다. 알 수 없는 임시 값은 현재 함수 scope의 temp provenance로 표시하며
 * 이 값은 LAT metadata `objects`에 등록되지 않을 수 있다.
 *
 * @param Ptr     null이 아닌 load/store 또는 GEP pointer operand. 소유권은
 *                LLVM IR이 유지하며 함수는 값을 빌리기만 한다.
 * @param Current 현재 분석 중인 함수
 * @param names   debug name map
 * @return storage object 또는 임시 pointer provenance를 나타내는 candidate ID
 */
std::string getObjectId(llvm::Value* Ptr, const llvm::Function& Current,
                        const NameMap& names);

}  // namespace lat
