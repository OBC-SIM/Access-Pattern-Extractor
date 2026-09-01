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

TEST(AccessBuilder, ScalarAccessCarriesCanonicalStorageObject)
{
  LLVMContext Ctx;
  Module M("scalar_identity", Ctx);
  IRBuilder<> Builder(Ctx);
  auto * I32 = Type::getInt32Ty(Ctx);
  auto * Global = new GlobalVariable(
    M, I32, false, GlobalValue::ExternalLinkage, nullptr, "global_value");
  auto * FunctionTy = FunctionType::get(Type::getVoidTy(Ctx),
                                        {PointerType::getUnqual(I32)}, false);
  Function * F = Function::Create(FunctionTy, Function::ExternalLinkage,
                                  "scalar_accesses", &M);
  Argument * Parameter = &*F->arg_begin();
  Parameter->setName("parameter");
  BasicBlock * BB = BasicBlock::Create(Ctx, "entry", F);
  Builder.SetInsertPoint(BB);
  auto * Local = Builder.CreateAlloca(I32, nullptr, "local_value");
  auto * GlobalLoad = Builder.CreateLoad(I32, Global);
  auto * LocalLoad = Builder.CreateLoad(I32, Local);
  auto * ParameterLoad = Builder.CreateLoad(I32, Parameter);
  Builder.CreateRetVoid();

  NameMap names;
  names[Local] = "local_value";
  names[Parameter] = "parameter";
  const auto metadata = buildAccessMetadata(M);
  const std::set<const Function *> inlineFuncs;
  DominatorTree DT(*F);
  LoopInfo LI(DT);
  AssumptionCache AC(*F);
  TargetLibraryInfoImpl TLII;
  TargetLibraryInfo TLI(TLII);
  ScalarEvolution SE(*F, TLI, AC, DT, LI);

  const auto object_id = [&](Instruction & instruction) {
    auto statement =
      makeAccessFromInstr(instruction, SE, names, metadata, inlineFuncs, *F);
    const auto * access = dynamic_cast<ScalarAccess *>(statement.get());
    EXPECT_NE(access, nullptr);
    if (access == nullptr) return std::string{};
    EXPECT_EQ(metadata.objects.count(access->getObjectId()), 1u);
    return access->getObjectId();
  };

  EXPECT_EQ(object_id(*GlobalLoad), "global::global_value");
  EXPECT_EQ(object_id(*LocalLoad),
            "function:scalar_accesses::local:local_value");
  EXPECT_EQ(object_id(*ParameterLoad),
            "function:scalar_accesses::param:parameter");
}

// GEP 경로의 scalar 분기는 describeGepAccess가 인덱스도 access_path도
// 만들지 못할 때만 도달한다. struct를 source element type으로 갖는 GEP에
// 비상수 인덱스 하나만 주면 consumeGepIndex가 아무것도 push하지 않아
// 그 조건이 성립한다.
TEST(AccessBuilder, GepScalarAccessResolvesParameterStorageObject)
{
  LLVMContext Ctx;
  Module M("gep_scalar_param", Ctx);
  IRBuilder<> Builder(Ctx);
  auto * I32 = Type::getInt32Ty(Ctx);
  auto * StructTy = StructType::create(Ctx, "struct.S");
  StructTy->setBody({I32, I32});
  auto * StructPtrTy = PointerType::getUnqual(StructTy);

  auto * FunctionTy =
    FunctionType::get(Type::getVoidTy(Ctx), {StructPtrTy, I32}, false);
  Function * F = Function::Create(FunctionTy, Function::ExternalLinkage,
                                  "gep_scalar_param", &M);
  auto Arg = F->arg_begin();
  Argument * Base = &*Arg++;
  Base->setName("p");
  Argument * Index = &*Arg;
  Index->setName("i");
  BasicBlock * BB = BasicBlock::Create(Ctx, "entry", F);
  Builder.SetInsertPoint(BB);
  auto * Element = Builder.CreateGEP(StructTy, Base, Index);
  auto * Load = Builder.CreateLoad(StructTy, Element);
  Builder.CreateRetVoid();

  NameMap names;
  names[Base] = "p";
  names[Index] = "i";
  const AccessMetadata metadata = buildAccessMetadata(M);
  std::set<const Function *> inlineFuncs;
  DominatorTree DT(*F);
  LoopInfo LI(DT);
  AssumptionCache AC(*F);
  TargetLibraryInfoImpl TLII;
  TargetLibraryInfo TLI(TLII);
  ScalarEvolution SE(*F, TLI, AC, DT, LI);

  auto stmt = makeAccessFromInstr(*Load, SE, names, metadata, inlineFuncs, *F);
  auto * access = dynamic_cast<ScalarAccess *>(stmt.get());

  ASSERT_NE(access, nullptr);
  EXPECT_EQ(access->getObjectId(), "function:gep_scalar_param::param:p");
  EXPECT_EQ(metadata.objects.count(access->getObjectId()), 1u);
}

TEST(AccessBuilder, GepScalarAccessOmitsUnknownRuntimeStorageObject)
{
  LLVMContext Ctx;
  Module M("gep_scalar_temp", Ctx);
  IRBuilder<> Builder(Ctx);
  auto * I32 = Type::getInt32Ty(Ctx);
  auto * StructTy = StructType::create(Ctx, "struct.S");
  StructTy->setBody({I32, I32});
  auto * StructPtrTy = PointerType::getUnqual(StructTy);

  auto * FunctionTy = FunctionType::get(
    Type::getVoidTy(Ctx), {PointerType::getUnqual(StructPtrTy), I32}, false);
  Function * F = Function::Create(FunctionTy, Function::ExternalLinkage,
                                  "gep_scalar_temp", &M);
  auto Arg = F->arg_begin();
  Argument * Handle = &*Arg++;
  Handle->setName("pp");
  Argument * Index = &*Arg;
  Index->setName("i");
  BasicBlock * BB = BasicBlock::Create(Ctx, "entry", F);
  Builder.SetInsertPoint(BB);
  auto * Base = Builder.CreateLoad(StructPtrTy, Handle, "loaded");
  auto * Element = Builder.CreateGEP(StructTy, Base, Index);
  auto * Load = Builder.CreateLoad(StructTy, Element);
  Builder.CreateRetVoid();

  NameMap names;
  names[Handle] = "pp";
  names[Index] = "i";
  const AccessMetadata metadata = buildAccessMetadata(M);
  std::set<const Function *> inlineFuncs;
  DominatorTree DT(*F);
  LoopInfo LI(DT);
  AssumptionCache AC(*F);
  TargetLibraryInfoImpl TLII;
  TargetLibraryInfo TLI(TLII);
  ScalarEvolution SE(*F, TLI, AC, DT, LI);

  auto stmt = makeAccessFromInstr(*Load, SE, names, metadata, inlineFuncs, *F);
  auto * access = dynamic_cast<ScalarAccess *>(stmt.get());

  ASSERT_NE(access, nullptr);
  EXPECT_TRUE(access->getObjectId().empty());
  EXPECT_EQ(metadata.objects.count("function:gep_scalar_temp::temp:loaded"),
            0u);
}

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

TEST(AccessBuilder, PreservesFieldOnlyGepAsStructuredAccess)
{
  LLVMContext Ctx;
  Module M("field_only", Ctx);
  IRBuilder<> Builder(Ctx);
  auto * I32 = Type::getInt32Ty(Ctx);
  auto * F64 = Type::getDoubleTy(Ctx);
  auto * StructTy = StructType::create(Ctx, "struct.S");
  StructTy->setBody({I32, F64});
  auto * Global = new GlobalVariable(
    M, StructTy, false, GlobalValue::ExternalLinkage, nullptr, "s");

  auto * FunctionTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function * F =
    Function::Create(FunctionTy, Function::ExternalLinkage, "field_only", &M);
  BasicBlock * BB = BasicBlock::Create(Ctx, "entry", F);
  Builder.SetInsertPoint(BB);
  auto * Field = Builder.CreateStructGEP(StructTy, Global, 1);
  auto * Load = Builder.CreateLoad(F64, Field);
  Builder.CreateRetVoid();

  NameMap names;
  AccessMetadata metadata = buildAccessMetadata(M);
  metadata.structs.at("S").fields[1].name = "y";
  std::set<const Function *> inlineFuncs;
  DominatorTree DT(*F);
  LoopInfo LI(DT);
  AssumptionCache AC(*F);
  TargetLibraryInfoImpl TLII;
  TargetLibraryInfo TLI(TLII);
  ScalarEvolution SE(*F, TLI, AC, DT, LI);

  auto stmt = makeAccessFromInstr(*Load, SE, names, metadata, inlineFuncs, *F);
  auto * access = dynamic_cast<ArrayAccess *>(stmt.get());

  ASSERT_NE(access, nullptr);
  EXPECT_EQ(access->getArrayName(), "s.y");
  EXPECT_TRUE(access->getIndexVars().empty());
  EXPECT_EQ(access->getObjectId(), "global::s");
  ASSERT_EQ(access->getAccessPath().size(), 1u);
  EXPECT_EQ(access->getAccessPath()[0].kind, "field");
  EXPECT_EQ(access->getAccessPath()[0].name, "y");
  EXPECT_EQ(access->getAccessPath()[0].index, 1);
}
