#include <set>

#include <gtest/gtest.h>

#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

#include "../include/AccessBuilder.hpp"
#include "../include/AccessMetadataBuilder.hpp"
#include "../include/JsonExportVisitor.hpp"
#include "../include/Statement.hpp"

using namespace lat;
using namespace llvm;

TEST(AccessObjectBinding, OmitsGlobalExcludedFromMetadata)
{
  LLVMContext Ctx;
  Module M("excluded_global", Ctx);
  IRBuilder<> Builder(Ctx);
  auto * Initializer = ConstantDataArray::getString(Ctx, "hello");
  auto * Literal = new GlobalVariable(
    M, Initializer->getType(), true, GlobalValue::PrivateLinkage,
    Initializer, ".str");

  auto * I32 = Type::getInt32Ty(Ctx);
  auto * FT = FunctionType::get(Type::getVoidTy(Ctx), {I32}, false);
  Function * F = llvm::Function::Create(
    FT, Function::ExternalLinkage, "literal_access", &M);
  Argument * Index = &*F->arg_begin();
  Index->setName("i");
  Builder.SetInsertPoint(BasicBlock::Create(Ctx, "entry", F));
  auto * Element = Builder.CreateInBoundsGEP(
    Initializer->getType(), Literal, {Builder.getInt32(0), Index});
  auto * Load = Builder.CreateLoad(Type::getInt8Ty(Ctx), Element);
  Builder.CreateRetVoid();

  NameMap names;
  names[Index] = "i";
  const AccessMetadata metadata = buildAccessMetadata(M);
  EXPECT_EQ(getObjectId(Literal, *F, names), "global::.str");
  EXPECT_EQ(metadata.objects.count("global::.str"), 0u);

  const std::set<const llvm::Function *> inlineFunctions;
  DominatorTree DT(*F);
  LoopInfo LI(DT);
  AssumptionCache AC(*F);
  TargetLibraryInfoImpl TLII;
  TargetLibraryInfo TLI(TLII);
  ScalarEvolution SE(*F, TLI, AC, DT, LI);

  auto statement = makeAccessFromInstr(
    *Load, SE, names, metadata, inlineFunctions, *F);
  auto * access = dynamic_cast<ArrayAccess *>(statement.get());
  ASSERT_NE(access, nullptr);
  EXPECT_TRUE(access->getObjectId().empty());

  JsonExportVisitor visitor;
  access->accept(visitor);
  auto * object = visitor.getResult().getAsObject();
  ASSERT_NE(object, nullptr);
  EXPECT_FALSE(object->getString("object").hasValue());
}
