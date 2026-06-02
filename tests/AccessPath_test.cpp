#include <gtest/gtest.h>

#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

#include "../include/AccessPath.hpp"

using namespace lat;
using namespace llvm;

namespace {

struct SEContext {
    DominatorTree DT;
    LoopInfo LI;
    AssumptionCache AC;
    TargetLibraryInfoImpl TLII;
    TargetLibraryInfo TLI;
    ScalarEvolution SE;

    explicit SEContext(Function& F)
        : DT(F), LI(DT), AC(F), TLI(TLII), SE(F, TLI, AC, DT, LI) {}
};

static void addField(StructMetadata& structure, std::string name) {
    FieldMetadata field;
    field.name = std::move(name);
    field.index = static_cast<int64_t>(structure.fields.size());
    structure.fields.push_back(std::move(field));
}

static AccessMetadata namedStructMetadata() {
    AccessMetadata metadata;
    StructMetadata outer;
    outer.name = "Outer";
    addField(outer, "tag");
    addField(outer, "items");
    metadata.structs[outer.name] = std::move(outer);

    StructMetadata inner;
    inner.name = "S";
    addField(inner, "x");
    addField(inner, "y");
    metadata.structs[inner.name] = std::move(inner);
    return metadata;
}

}  // namespace

TEST(AccessPath, PreservesStructArrayFieldOrder) {
    LLVMContext Ctx;
    Module M("access_path", Ctx);
    IRBuilder<> Builder(Ctx);
    auto* I32 = Type::getInt32Ty(Ctx);
    auto* F64 = Type::getDoubleTy(Ctx);
    auto* STy = StructType::create(Ctx, "struct.S");
    STy->setBody({I32, F64});
    auto* ItemsTy = ArrayType::get(STy, 4);
    auto* OuterTy = StructType::create(Ctx, "struct.Outer");
    OuterTy->setBody({I32, ItemsTy});
    auto* PtrTy = PointerType::getUnqual(OuterTy);
    auto* FT = FunctionType::get(Type::getVoidTy(Ctx), {PtrTy, I32}, false);
    Function* F = Function::Create(FT, Function::ExternalLinkage, "probe", &M);
    auto argIt = F->arg_begin();
    Argument* Obj = &*argIt++;
    Argument* Index = &*argIt;
    Obj->setName("o");
    Index->setName("i");

    Builder.SetInsertPoint(BasicBlock::Create(Ctx, "entry", F));
    auto* Zero = ConstantInt::get(I32, 0);
    auto* Items = ConstantInt::get(I32, 1);
    auto* FieldX = ConstantInt::get(I32, 0);
    auto* Gep = cast<GetElementPtrInst>(
        Builder.CreateInBoundsGEP(OuterTy, Obj, {Zero, Items, Index, FieldX}));
    Builder.CreateRetVoid();

    NameMap names;
    names[Obj] = "o";
    names[Index] = "i";
    AccessMetadata metadata = namedStructMetadata();
    SEContext SECtx(*F);

    auto desc = describeGepAccess(cast<GEPOperator>(Gep), SECtx.SE, names, metadata);

    EXPECT_EQ(desc.name, "o.items[i].x");
    EXPECT_EQ(desc.indices, (std::vector<std::string>{"i"}));
    ASSERT_EQ(desc.access_path.size(), 3u);
    EXPECT_EQ(desc.access_path[0].kind, "field");
    EXPECT_EQ(desc.access_path[0].name, "items");
    EXPECT_EQ(desc.access_path[0].index, 1);
    EXPECT_EQ(desc.access_path[1].kind, "index");
    EXPECT_EQ(desc.access_path[1].value, "i");
    EXPECT_EQ(desc.access_path[2].kind, "field");
    EXPECT_EQ(desc.access_path[2].name, "x");
    EXPECT_EQ(desc.access_path[2].index, 0);
}

TEST(AccessPath, PreservesTwoDimensionalArrayIndices) {
    LLVMContext Ctx;
    Module M("access_path", Ctx);
    IRBuilder<> Builder(Ctx);
    auto* I32 = Type::getInt32Ty(Ctx);
    auto* RowTy = ArrayType::get(I32, 16);
    auto* MatrixTy = ArrayType::get(RowTy, 8);
    auto* PtrTy = PointerType::getUnqual(MatrixTy);
    auto* FT = FunctionType::get(Type::getVoidTy(Ctx), {PtrTy, I32, I32}, false);
    Function* F = Function::Create(FT, Function::ExternalLinkage, "probe", &M);
    auto argIt = F->arg_begin();
    Argument* Matrix = &*argIt++;
    Argument* I = &*argIt++;
    Argument* J = &*argIt;
    Matrix->setName("A");
    I->setName("i");
    J->setName("j");

    Builder.SetInsertPoint(BasicBlock::Create(Ctx, "entry", F));
    auto* Zero = ConstantInt::get(I32, 0);
    auto* Gep = cast<GetElementPtrInst>(
        Builder.CreateInBoundsGEP(MatrixTy, Matrix, {Zero, I, J}));
    Builder.CreateRetVoid();

    NameMap names;
    names[Matrix] = "A";
    names[I] = "i";
    names[J] = "j";
    AccessMetadata metadata;
    SEContext SECtx(*F);

    auto desc = describeGepAccess(cast<GEPOperator>(Gep), SECtx.SE, names, metadata);

    EXPECT_EQ(desc.name, "A[i][j]");
    EXPECT_EQ(desc.indices, (std::vector<std::string>{"i", "j"}));
    ASSERT_EQ(desc.access_path.size(), 2u);
    EXPECT_EQ(desc.access_path[0].kind, "index");
    EXPECT_EQ(desc.access_path[0].value, "i");
    EXPECT_EQ(desc.access_path[1].kind, "index");
    EXPECT_EQ(desc.access_path[1].value, "j");
}
