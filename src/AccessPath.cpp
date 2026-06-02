#include "../include/AccessPath.hpp"

#include "llvm/IR/Argument.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

namespace lat {
namespace {

static Value* baseObject(Value* V) {
    V = V->stripPointerCasts();
    while (auto* GEP = dyn_cast<GEPOperator>(V))
        V = GEP->getPointerOperand()->stripPointerCasts();
    return V;
}

static std::string objectName(Value* Base, const NameMap& names) {
    auto it = names.find(Base);
    if (it != names.end())
        return it->second;
    return getBaseName(Base, names);
}

static std::string structName(StructType* Ty) {
    std::string name = Ty->hasName() ? Ty->getName().str() : "";
    constexpr char prefix[] = "struct.";
    if (name.rfind(prefix, 0) == 0)
        return name.substr(sizeof(prefix) - 1);
    return name;
}

static std::string fieldName(StructType* Ty, uint64_t index,
                             const AccessMetadata& metadata) {
    auto it = metadata.structs.find(structName(Ty));
    if (it == metadata.structs.end() || index >= it->second.fields.size())
        return "field_" + std::to_string(index);
    return it->second.fields[index].name;
}

static AccessPathSegment fieldSegment(std::string name, uint64_t index) {
    AccessPathSegment segment;
    segment.kind = "field";
    segment.name = std::move(name);
    segment.index = static_cast<int64_t>(index);
    return segment;
}

static AccessPathSegment indexSegment(std::string value) {
    AccessPathSegment segment;
    segment.kind = "index";
    segment.value = std::move(value);
    return segment;
}

static void appendResolvedIndex(Value* Idx, ScalarEvolution& SE,
                                const NameMap& names,
                                GepAccessDescription& out) {
    for (auto value : resolveIndex(Idx, SE, names)) {
        out.indices.push_back(value);
        out.access_path.push_back(indexSegment(value));
        out.name += "[" + value + "]";
    }
}

static Type* consumeGepIndex(Type* Ty, Value* Idx, ScalarEvolution& SE,
                             const NameMap& names,
                             const AccessMetadata& metadata,
                             GepAccessDescription& out) {
    if (auto* StructTy = dyn_cast<StructType>(Ty)) {
        if (auto* C = dyn_cast<ConstantInt>(Idx)) {
            uint64_t field = C->getZExtValue();
            std::string name = fieldName(StructTy, field, metadata);
            out.name += "." + name;
            out.access_path.push_back(fieldSegment(std::move(name), field));
            return StructTy->getElementType(static_cast<unsigned>(field));
        }
        return Ty;
    }
    if (auto* ArrayTy = dyn_cast<ArrayType>(Ty)) {
        appendResolvedIndex(Idx, SE, names, out);
        return ArrayTy->getElementType();
    }
    appendResolvedIndex(Idx, SE, names, out);
    return Ty;
}

static void collectGepChain(GEPOperator* GEP, std::vector<GEPOperator*>& chain) {
    if (auto* Parent = dyn_cast<GEPOperator>(GEP->getPointerOperand()->stripPointerCasts()))
        collectGepChain(Parent, chain);
    chain.push_back(GEP);
}

}  // namespace

GepAccessDescription describeGepAccess(GEPOperator* GEP, ScalarEvolution& SE,
                                       const NameMap& names,
                                       const AccessMetadata& metadata) {
    std::vector<GEPOperator*> chain;
    collectGepChain(GEP, chain);

    GepAccessDescription result;
    result.name = objectName(baseObject(GEP->getPointerOperand()), names);

    for (GEPOperator* Current : chain) {
        Type* Ty = Current->getSourceElementType();
        auto it = Current->idx_begin();
        if (Current->getNumIndices() > 1 && isa<ConstantInt>(*it) &&
            cast<ConstantInt>(*it)->isZero())
            ++it;
        for (; it != Current->idx_end(); ++it)
            Ty = consumeGepIndex(Ty, *it, SE, names, metadata, result);
    }
    return result;
}

}  // namespace lat
