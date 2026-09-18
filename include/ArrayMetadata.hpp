#pragma once

#include <cstdint>
#include <vector>

namespace map {

struct ArrayMetadata {
    std::vector<int64_t> shape;
    int64_t elem_size = 0;
};

}  // namespace map
