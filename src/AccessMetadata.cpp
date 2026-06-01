#include "../include/AccessMetadata.hpp"

namespace lat {

static void addTypeFields(llvm::json::Object& obj, const std::string& kind,
                          const std::vector<int64_t>& shape,
                          const std::string& elem_type, int64_t elem_size,
                          const std::string& llvm_type) {
    obj["kind"] = kind;
    if (!shape.empty()) {
        llvm::json::Array dims;
        for (int64_t dim : shape)
            dims.push_back(dim);
        obj["shape"] = std::move(dims);
    }
    if (!elem_type.empty())
        obj["elem_type"] = elem_type;
    if (elem_size > 0)
        obj["elem_size"] = elem_size;
    if (!llvm_type.empty())
        obj["llvm_type"] = llvm_type;
}

llvm::json::Object toJson(const FieldMetadata& field) {
    llvm::json::Object obj{
        {"name", field.name},
        {"index", field.index},
        {"offset", field.offset},
        {"size", field.size}
    };
    addTypeFields(obj, field.kind, field.shape, field.elem_type,
                  field.elem_size, field.llvm_type);
    if (!field.source_type.empty())
        obj["source_type"] = field.source_type;
    return obj;
}

llvm::json::Object toJson(const StructMetadata& structure) {
    llvm::json::Array fields;
    for (const FieldMetadata& field : structure.fields)
        fields.push_back(toJson(field));
    return llvm::json::Object{
        {"name", structure.name},
        {"size", structure.size},
        {"align", structure.align},
        {"fields", std::move(fields)}
    };
}

llvm::json::Object toJson(const ObjectMetadata& object) {
    llvm::json::Object obj{
        {"id", object.id},
        {"name", object.name},
        {"scope", object.scope},
        {"storage", object.storage}
    };
    addTypeFields(obj, object.kind, object.shape, object.elem_type,
                  object.elem_size, object.llvm_type);
    return obj;
}

llvm::json::Object toJson(const AccessMetadata& metadata) {
    llvm::json::Object objects;
    for (const auto& entry : metadata.objects)
        objects[entry.first] = toJson(entry.second);

    llvm::json::Object structs;
    for (const auto& entry : metadata.structs)
        structs[entry.first] = toJson(entry.second);

    return llvm::json::Object{
        {"objects", std::move(objects)},
        {"structs", std::move(structs)}
    };
}

}  // namespace lat
