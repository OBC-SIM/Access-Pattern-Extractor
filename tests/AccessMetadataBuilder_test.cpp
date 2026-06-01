#include <gtest/gtest.h>

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

#include "../include/AccessMetadataBuilder.hpp"

using namespace lat;
using namespace llvm;

TEST(AccessMetadataBuilder, CollectsStructLayout) {
    LLVMContext Ctx;
    Module M("metadata", Ctx);
    M.setDataLayout("e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128");
    auto* StructTy = StructType::create(Ctx, "struct.S");
    StructTy->setBody({Type::getInt32Ty(Ctx), Type::getDoubleTy(Ctx)});
    new GlobalVariable(M, StructTy, false, GlobalValue::ExternalLinkage, nullptr, "G");

    AccessMetadata metadata = buildAccessMetadata(M);

    auto it = metadata.structs.find("S");
    ASSERT_NE(it, metadata.structs.end());
    EXPECT_EQ(it->second.size, 16);
    EXPECT_EQ(it->second.align, 8);
    ASSERT_EQ(it->second.fields.size(), 2u);
    EXPECT_EQ(it->second.fields[0].name, "field_0");
    EXPECT_EQ(it->second.fields[0].offset, 0);
    EXPECT_EQ(it->second.fields[0].size, 4);
    EXPECT_EQ(it->second.fields[0].kind, "scalar");
    EXPECT_EQ(it->second.fields[0].elem_type, "i32");
    EXPECT_EQ(it->second.fields[0].elem_size, 4);
    EXPECT_EQ(it->second.fields[0].llvm_type, "i32");
    EXPECT_EQ(it->second.fields[1].name, "field_1");
    EXPECT_EQ(it->second.fields[1].offset, 8);
    EXPECT_EQ(it->second.fields[1].size, 8);
    EXPECT_EQ(it->second.fields[1].kind, "scalar");
    EXPECT_EQ(it->second.fields[1].elem_type, "double");
    EXPECT_EQ(it->second.fields[1].elem_size, 8);
}

TEST(AccessMetadataBuilder, CollectsGlobalAndParamObjects) {
    LLVMContext Ctx;
    Module M("metadata", Ctx);
    IRBuilder<> Builder(Ctx);
    auto* I32 = Type::getInt32Ty(Ctx);
    auto* ArrayTy = ArrayType::get(I32, 8);
    new GlobalVariable(M, ArrayTy, false, GlobalValue::ExternalLinkage, nullptr, "A");

    auto* PtrTy = PointerType::getUnqual(I32);
    auto* FT = FunctionType::get(Type::getVoidTy(Ctx), {PtrTy}, false);
    Function* F = Function::Create(FT, Function::ExternalLinkage, "foo", &M);
    F->arg_begin()->setName("A");
    Builder.SetInsertPoint(BasicBlock::Create(Ctx, "entry", F));
    Builder.CreateRetVoid();

    AccessMetadata metadata = buildAccessMetadata(M);

    EXPECT_NE(metadata.objects.find("global::A"), metadata.objects.end());
    auto param = metadata.objects.find("function:foo::param:A");
    ASSERT_NE(param, metadata.objects.end());
    EXPECT_EQ(param->second.name, "A");
    EXPECT_EQ(param->second.scope, "function:foo");
    EXPECT_EQ(param->second.storage, "param");
    EXPECT_EQ(param->second.kind, "pointer");
    EXPECT_EQ(param->second.elem_type, "i32");
    EXPECT_EQ(param->second.elem_size, 4);

    auto global = metadata.objects.find("global::A");
    ASSERT_NE(global, metadata.objects.end());
    EXPECT_EQ(global->second.kind, "array");
    EXPECT_EQ(global->second.shape, (std::vector<int64_t>{8}));
    EXPECT_EQ(global->second.elem_type, "i32");
    EXPECT_EQ(global->second.elem_size, 4);
    EXPECT_EQ(global->second.llvm_type, "[8 x i32]");
}

TEST(AccessMetadataBuilder, DistinguishesObjectIdsByScope) {
    LLVMContext Ctx;
    Module M("metadata", Ctx);
    IRBuilder<> Builder(Ctx);
    auto* I32 = Type::getInt32Ty(Ctx);
    auto* FT = FunctionType::get(Type::getVoidTy(Ctx), {PointerType::getUnqual(I32)}, false);
    Function* F = Function::Create(FT, Function::ExternalLinkage, "foo", &M);
    F->arg_begin()->setName("A");
    BasicBlock* BB = BasicBlock::Create(Ctx, "entry", F);
    Builder.SetInsertPoint(BB);
    AllocaInst* Local = Builder.CreateAlloca(I32, nullptr, "A");
    Builder.CreateRetVoid();

    NameMap names;
    names[Local] = "A";

    EXPECT_EQ(getObjectId(&*F->arg_begin(), *F, names), "function:foo::param:A");
    EXPECT_EQ(getObjectId(Local, *F, names), "function:foo::local:A");
}
