#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Operator.h"

#include "AccessMetadata.hpp"
#include "IrHelpers.hpp"

namespace lat {

/**
 * @brief Source-level access path에서 한 단계의 field 또는 index 이동을 나타낸다.
 *
 * Field segment는 구조체 field index/name을 보존하고, index segment는 배열 또는
 * pointer index 식을 보존한다. Segment 순서는 GEP가 실제 주소를 계산하는 순서와
 * 같다.
 */
struct AccessPathSegment {
    std::string kind;
    std::string name;
    int64_t index = -1;
    std::string value;
};

/**
 * @brief GEP 접근을 display name, legacy indices, structured path로 분해한 결과.
 */
struct GepAccessDescription {
    std::string name;
    std::vector<std::string> indices;
    std::vector<AccessPathSegment> access_path;
};

/**
 * @brief GEP 접근을 구조화된 source access path로 변환한다.
 *
 * `indices`는 기존 downstream 호환을 위해 배열/pointer index만 유지한다.
 * `access_path`는 구조체 field와 index의 실제 순서를 모두 보존한다.
 *
 * @param GEP      분석할 GEP
 * @param SE       ScalarEvolution 분석 결과
 * @param names    debug name map
 * @param metadata root metadata
 * @return display name, legacy indices, structured access path
 */
GepAccessDescription describeGepAccess(llvm::GEPOperator* GEP,
                                       llvm::ScalarEvolution& SE,
                                       const NameMap& names,
                                       const AccessMetadata& metadata);

}  // namespace lat
