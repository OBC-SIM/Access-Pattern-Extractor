#pragma once

#include <string>

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Type.h"

#include "AccessMetadata.hpp"

namespace lat {

/**
 * @brief LLVM type을 사람이 읽을 수 있는 IR 문자열로 변환한다.
 *
 * @param Ty 변환할 LLVM type
 * @return LLVM IR type 문자열
 */
std::string typeString(llvm::Type* Ty);

/**
 * @brief LLVM 구조체 이름에서 clang prefix를 제거한다.
 *
 * @param Name LLVM identified struct name
 * @return `struct.` 또는 `class.` prefix가 제거된 이름
 */
std::string cleanStructName(llvm::StringRef Name);

/**
 * @brief ObjectMetadata에 구조화된 type 정보를 채운다.
 *
 * @param object 채울 object metadata
 * @param Ty     분석할 LLVM type
 * @param DL     ABI layout을 계산할 DataLayout
 */
void fillTypeInfo(ObjectMetadata& object, llvm::Type* Ty, const llvm::DataLayout& DL);

/**
 * @brief FieldMetadata에 구조화된 type 정보를 채운다.
 *
 * @param field 채울 field metadata
 * @param Ty    분석할 LLVM type
 * @param DL    ABI layout을 계산할 DataLayout
 */
void fillTypeInfo(FieldMetadata& field, llvm::Type* Ty, const llvm::DataLayout& DL);

}  // namespace lat
