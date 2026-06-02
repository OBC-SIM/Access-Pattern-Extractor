#include <set>

#include <gtest/gtest.h>

#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

#include "../include/AccessBuilder.hpp"
#include "../include/AccessMetadataBuilder.hpp"
#include "../include/Statement.hpp"

using namespace lat;
using namespace llvm;

TEST(AccessBuilder, InlineCallCarriesArgumentObjectRefs) {
    LLVMContext Ctx;
    Module M("access_builder", Ctx);
    IRBuilder<> Builder(Ctx);
    auto* F32 = Type::getFloatTy(Ctx);
    auto* I32 = Type::getInt32Ty(Ctx);
    auto* ArrayTy = ArrayType::get(F32, 16);
    auto* Global = new GlobalVariable(
        M, ArrayTy, false, GlobalValue::ExternalLinkage, nullptr, "A");

    auto* HelperTy = FunctionType::get(
        Type::getVoidTy(Ctx), {PointerType::getUnqual(ArrayTy), I32}, false);
    Function* Helper = Function::Create(
        HelperTy, Function::ExternalLinkage, "helper", &M);

    auto* CallerTy = FunctionType::get(Type::getVoidTy(Ctx), {I32}, false);
    Function* Caller = Function::Create(
        CallerTy, Function::ExternalLinkage, "caller", &M);
    Argument* Index = &*Caller->arg_begin();
    Index->setName("i");
    BasicBlock* BB = BasicBlock::Create(Ctx, "entry", Caller);
    Builder.SetInsertPoint(BB);
    CallInst* Call = Builder.CreateCall(Helper, {Global, Index});
    Builder.CreateRetVoid();

    NameMap names;
    names[Index] = "i";
    AccessMetadata metadata = buildAccessMetadata(M);
    std::set<const Function*> inlineFuncs{Helper};
    DominatorTree DT(*Caller);
    LoopInfo LI(DT);
    AssumptionCache AC(*Caller);
    TargetLibraryInfoImpl TLII;
    TargetLibraryInfo TLI(TLII);
    ScalarEvolution SE(*Caller, TLI, AC, DT, LI);

    auto stmt = makeAccessFromInstr(*Call, SE, names, metadata, inlineFuncs, *Caller);
    auto* call = dynamic_cast<CallStmt*>(stmt.get());
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->getArgs(), (std::vector<std::string>{"A", "i"}));
    EXPECT_EQ(call->getArgObjects(), (std::vector<std::string>{"global::A", "i"}));
}
