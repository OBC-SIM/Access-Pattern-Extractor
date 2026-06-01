#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "llvm/Support/JSON.h"

namespace lat {

/**
 * @brief 구조체 필드의 ABI layout과 source-level 이름을 보관한다.
 *
 * offset과 size는 byte 단위이며, source field name이 없으면 field_N
 * fallback 이름을 사용한다.
 */
struct FieldMetadata {
    std::string name;
    int64_t index = 0;
    int64_t offset = 0;
    int64_t size = 0;
    std::string kind;
    std::vector<int64_t> shape;
    std::string elem_type;
    int64_t elem_size = 0;
    std::string llvm_type;
    std::string source_type;
};

/**
 * @brief LLVM StructType 하나의 직렬화 가능한 layout metadata.
 */
struct StructMetadata {
    std::string name;
    int64_t size = 0;
    int64_t align = 0;
    std::vector<FieldMetadata> fields;
};

/**
 * @brief access node가 참조하는 base storage object의 metadata.
 */
struct ObjectMetadata {
    std::string id;
    std::string name;
    std::string scope;
    std::string storage;
    std::string kind;
    std::vector<int64_t> shape;
    std::string elem_type;
    int64_t elem_size = 0;
    std::string llvm_type;
};

/**
 * @brief LAT v2 root metadata에 들어갈 object와 structure layout 집합.
 */
struct AccessMetadata {
    std::map<std::string, ObjectMetadata> objects;
    std::map<std::string, StructMetadata> structs;
};

/**
 * @brief FieldMetadata를 JSON object로 변환한다.
 *
 * @param field 변환할 field metadata
 * @return LAT metadata schema에 맞춘 JSON object
 */
llvm::json::Object toJson(const FieldMetadata& field);

/**
 * @brief StructMetadata를 JSON object로 변환한다.
 *
 * @param structure 변환할 structure metadata
 * @return LAT metadata schema에 맞춘 JSON object
 */
llvm::json::Object toJson(const StructMetadata& structure);

/**
 * @brief ObjectMetadata를 JSON object로 변환한다.
 *
 * @param object 변환할 object metadata
 * @return LAT metadata schema에 맞춘 JSON object
 */
llvm::json::Object toJson(const ObjectMetadata& object);

/**
 * @brief AccessMetadata를 LAT v2 root metadata JSON으로 변환한다.
 *
 * @param metadata 변환할 metadata 집합
 * @return `objects`와 `structs`를 포함하는 JSON object
 */
llvm::json::Object toJson(const AccessMetadata& metadata);

}  // namespace lat
