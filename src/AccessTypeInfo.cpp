#include "../include/AccessTypeInfo.hpp"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace lat {
namespace {

static std::string structName(StructType* Ty) {
    return Ty->hasName() ? cleanStructName(Ty->getName()) : "";
}

static std::string elemTypeName(Type* Ty) {
    if (auto* StructTy = dyn_cast<StructType>(Ty))
        return structName(StructTy);
    return typeString(Ty);
}

template <typename MetadataT>
static void fillTypeInfoImpl(MetadataT& out, Type* Ty, const DataLayout& DL) {
    Type* Cursor = Ty;
    out.llvm_type = typeString(Ty);
    if (auto* PtrTy = dyn_cast<PointerType>(Cursor)) {
        out.kind = "pointer";
        if (!PtrTy->isOpaque())
            Cursor = PtrTy->getPointerElementType();
    }
    while (auto* ArrayTy = dyn_cast<ArrayType>(Cursor)) {
        if (out.kind.empty())
            out.kind = "array";
        out.shape.push_back(static_cast<int64_t>(ArrayTy->getNumElements()));
        Cursor = ArrayTy->getElementType();
    }
    if (out.kind.empty())
        out.kind = isa<StructType>(Cursor) ? "struct" : "scalar";
    out.elem_type = elemTypeName(Cursor);
    if (!Cursor->isVoidTy() && !Cursor->isFunctionTy())
        out.elem_size = static_cast<int64_t>(DL.getTypeAllocSize(Cursor).getFixedValue());
}

}  // namespace

std::string typeString(Type* Ty) {
    std::string s;
    raw_string_ostream os(s);
    Ty->print(os);
    os.flush();
    return s;
}

std::string cleanStructName(StringRef Name) {
    StringRef Clean = Name;
    Clean.consume_front("struct.");
    Clean.consume_front("class.");
    return Clean.str();
}

void fillTypeInfo(ObjectMetadata& object, Type* Ty, const DataLayout& DL) {
    fillTypeInfoImpl(object, Ty, DL);
}

void fillTypeInfo(FieldMetadata& field, Type* Ty, const DataLayout& DL) {
    fillTypeInfoImpl(field, Ty, DL);
}

}  // namespace lat
