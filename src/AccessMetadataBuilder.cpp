#include "../include/AccessMetadataBuilder.hpp"

#include <string>

#include "../include/AccessTypeInfo.hpp"

#include "llvm/IR/Argument.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"

using namespace llvm;

namespace lat {
namespace {

static std::string structName(StructType* Ty) {
    return Ty->hasName() ? cleanStructName(Ty->getName()) : "";
}

static Value* baseObject(Value* Ptr) {
    Value* Base = Ptr->stripPointerCasts();
    while (auto* GEP = dyn_cast<GEPOperator>(Base))
        Base = GEP->getPointerOperand()->stripPointerCasts();
    if (auto* Load = dyn_cast<LoadInst>(Base)) {
        Value* LoadPtr = Load->getPointerOperand()->stripPointerCasts();
        if (isa<AllocaInst>(LoadPtr))
            return LoadPtr;
    }
    return Base;
}

static std::string objectName(Value* Base, const NameMap& names) {
    if (auto it = names.find(Base); it != names.end())
        return it->second;
    if (Base->hasName())
        return Base->getName().str();
    return "tmp";
}

static ObjectMetadata makeObject(StringRef id, StringRef name, StringRef scope,
                                 StringRef storage, Type* Ty, const DataLayout& DL) {
    ObjectMetadata object;
    object.id = id.str();
    object.name = name.str();
    object.scope = scope.str();
    object.storage = storage.str();
    fillTypeInfo(object, Ty, DL);
    return object;
}

static void collectGlobals(Module& M, AccessMetadata& metadata) {
    const DataLayout& DL = M.getDataLayout();
    for (GlobalVariable& G : M.globals()) {
        if (G.getName().startswith("llvm.") || G.getName().startswith(".str") ||
            G.getSection() == "llvm.metadata")
            continue;
        std::string name = G.getName().str();
        std::string id = "global::" + name;
        metadata.objects[id] = makeObject(id, name, "global", "global", G.getValueType(), DL);
    }
}

static void collectFunctionObjects(Module& M, AccessMetadata& metadata) {
    const DataLayout& DL = M.getDataLayout();
    for (Function& F : M) {
        if (F.isDeclaration())
            continue;
        NameMap names = buildDebugNameMap(F);
        for (Argument& Arg : F.args()) {
            std::string name = objectName(&Arg, names);
            if (name == "tmp")
                name = "arg" + std::to_string(Arg.getArgNo());
            std::string id = "function:" + F.getName().str() + "::param:" + name;
            std::string scope = "function:" + F.getName().str();
            metadata.objects[id] = makeObject(id, name, scope, "param", Arg.getType(), DL);
        }
        for (BasicBlock& BB : F) {
            for (Instruction& I : BB) {
                auto* Alloca = dyn_cast<AllocaInst>(&I);
                if (!Alloca)
                    continue;
                std::string name = objectName(Alloca, names);
                std::string id = "function:" + F.getName().str() + "::local:" + name;
                std::string scope = "function:" + F.getName().str();
                metadata.objects[id] = makeObject(
                    id, name, scope, "local", Alloca->getAllocatedType(), DL);
            }
        }
    }
}

static StructMetadata makeStructMetadata(StructType* Ty, const DataLayout& DL) {
    const StructLayout* Layout = DL.getStructLayout(Ty);
    StructMetadata result;
    result.name = cleanStructName(Ty->getName());
    result.size = static_cast<int64_t>(DL.getTypeAllocSize(Ty).getFixedValue());
    result.align = static_cast<int64_t>(Layout->getAlignment().value());

    for (unsigned i = 0; i < Ty->getNumElements(); ++i) {
        Type* FieldTy = Ty->getElementType(i);
        FieldMetadata field;
        field.name = "field_" + std::to_string(i);
        field.index = static_cast<int64_t>(i);
        field.offset = static_cast<int64_t>(Layout->getElementOffset(i));
        field.size = static_cast<int64_t>(DL.getTypeAllocSize(FieldTy).getFixedValue());
        fillTypeInfo(field, FieldTy, DL);
        result.fields.push_back(std::move(field));
    }
    return result;
}

static void applyDebugComposite(DICompositeType* Struct, AccessMetadata& metadata);

static void visitDebugType(DIType* Ty, AccessMetadata& metadata) {
    if (!Ty)
        return;
    if (auto* Derived = dyn_cast<DIDerivedType>(Ty)) {
        visitDebugType(Derived->getBaseType(), metadata);
        return;
    }
    if (auto* Composite = dyn_cast<DICompositeType>(Ty)) {
        if (Composite->getTag() == dwarf::DW_TAG_structure_type)
            applyDebugComposite(Composite, metadata);
        visitDebugType(Composite->getBaseType(), metadata);
        for (Metadata* Element : Composite->getElements())
            if (auto* Derived = dyn_cast_or_null<DIDerivedType>(Element))
                visitDebugType(Derived->getBaseType(), metadata);
    }
}

static void applyDebugComposite(DICompositeType* Struct, AccessMetadata& metadata) {
    if (!Struct || Struct->getName().empty())
        return;
    auto it = metadata.structs.find(Struct->getName().str());
    if (it == metadata.structs.end())
        return;

    unsigned index = 0;
    for (Metadata* Element : Struct->getElements()) {
        auto* Member = dyn_cast_or_null<DIDerivedType>(Element);
        if (!Member || Member->getTag() != dwarf::DW_TAG_member)
            continue;
        if (index >= it->second.fields.size())
            break;
        if (!Member->getName().empty())
            it->second.fields[index].name = Member->getName().str();
        if (DIType* Base = Member->getBaseType())
            if (!Base->getName().empty())
                it->second.fields[index].source_type = Base->getName().str();
        ++index;
    }
}

static void applyDebugFieldNames(Module& M, AccessMetadata& metadata) {
    for (Function& F : M) {
        if (DISubprogram* SP = F.getSubprogram()) {
            if (DISubroutineType* SubTy = SP->getType())
                for (DIType* Ty : SubTy->getTypeArray())
                    visitDebugType(Ty, metadata);
        }
    }
    for (const DICompileUnit* CU : M.debug_compile_units())
        for (DINode* Node : CU->getRetainedTypes())
            visitDebugType(dyn_cast_or_null<DIType>(Node), metadata);
}

static std::string fieldName(StructType* Ty, uint64_t index,
                             const AccessMetadata& metadata) {
    auto it = metadata.structs.find(structName(Ty));
    if (it == metadata.structs.end() || index >= it->second.fields.size())
        return "field_" + std::to_string(index);
    return it->second.fields[index].name;
}

static void appendResolvedIndex(Value* Idx, ScalarEvolution& SE,
                                const NameMap& names,
                                std::vector<std::string>& out) {
    auto resolved = resolveIndex(Idx, SE, names);
    out.insert(out.end(), resolved.begin(), resolved.end());
}

static Type* consumeGepIndex(Type* Ty, Value* Idx, ScalarEvolution& SE,
                             const NameMap& names, const AccessMetadata& metadata,
                             std::string& name, std::vector<std::string>& indices) {
    if (auto* StructTy = dyn_cast<StructType>(Ty)) {
        if (auto* C = dyn_cast<ConstantInt>(Idx)) {
            uint64_t field = C->getZExtValue();
            name += "." + fieldName(StructTy, field, metadata);
            return StructTy->getElementType(static_cast<unsigned>(field));
        }
        return Ty;
    }
    if (auto* ArrayTy = dyn_cast<ArrayType>(Ty)) {
        appendResolvedIndex(Idx, SE, names, indices);
        return ArrayTy->getElementType();
    }
    appendResolvedIndex(Idx, SE, names, indices);
    return Ty;
}

static void collectGepChain(GEPOperator* GEP, std::vector<GEPOperator*>& chain) {
    if (auto* Parent = dyn_cast<GEPOperator>(GEP->getPointerOperand()->stripPointerCasts()))
        collectGepChain(Parent, chain);
    chain.push_back(GEP);
}

}  // namespace

AccessMetadata buildAccessMetadata(Module& M) {
    AccessMetadata metadata;
    collectGlobals(M, metadata);
    collectFunctionObjects(M, metadata);

    const DataLayout& DL = M.getDataLayout();
    for (StructType* Ty : M.getIdentifiedStructTypes()) {
        if (Ty->isOpaque())
            continue;
        StructMetadata structure = makeStructMetadata(Ty, DL);
        metadata.structs[structure.name] = std::move(structure);
    }
    applyDebugFieldNames(M, metadata);
    return metadata;
}

std::string getObjectId(Value* Ptr, const Function& Current, const NameMap& names) {
    Value* Base = baseObject(Ptr);
    std::string name = objectName(Base, names);
    if (auto* G = dyn_cast<GlobalVariable>(Base))
        return "global::" + G->getName().str();
    if (auto* Arg = dyn_cast<Argument>(Base))
        return "function:" + Arg->getParent()->getName().str() + "::param:" + name;
    if (isa<AllocaInst>(Base))
        return "function:" + Current.getName().str() + "::local:" + name;
    return "function:" + Current.getName().str() + "::temp:" + name;
}

std::pair<std::string, std::vector<std::string>> describeGepAccess(
    GEPOperator* GEP,
    ScalarEvolution& SE,
    const NameMap& names,
    const AccessMetadata& metadata) {
    std::vector<GEPOperator*> chain;
    collectGepChain(GEP, chain);

    Value* base = baseObject(GEP->getPointerOperand());
    std::string name = objectName(base, names);
    std::vector<std::string> indices;

    for (GEPOperator* Current : chain) {
        Type* Ty = Current->getSourceElementType();
        auto it = Current->idx_begin();
        if (Current->getNumIndices() > 1 && isa<ConstantInt>(*it) &&
            cast<ConstantInt>(*it)->isZero())
            ++it;
        for (; it != Current->idx_end(); ++it)
            Ty = consumeGepIndex(Ty, *it, SE, names, metadata, name, indices);
    }
    return {name, indices};
}

}  // namespace lat
