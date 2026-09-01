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
 * 피한다. load, select, phi 등 런타임 값에서 유도되어 canonical storage를
 * 정적으로 식별할 수 없는 base는 빈 문자열로 나타낸다.
 *
 * @param Ptr     null이 아닌 load/store 또는 GEP pointer operand. 소유권은
 *                LLVM IR이 유지하며 함수는 값을 빌리기만 한다.
 * @param Current 현재 분석 중인 함수
 * @param names   debug name map
 * @return canonical storage candidate ID. 식별할 수 없으면 빈 문자열
 * @note JSON에 ID를 기록하기 전에 `metadata.objects` 등록 여부를 확인해야 한다.
 */
std::string getObjectId(llvm::Value* Ptr, const llvm::Function& Current,
                        const NameMap& names);

}  // namespace lat
