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

static std::string str(llvm::Optional<llvm::StringRef> opt) {
    return opt ? opt->str() : "";
}

static int64_t i64(llvm::Optional<int64_t> opt) {
    return opt.getValueOr(0);
}

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

TEST(AccessMetadataBuilder, OmitsRuntimeDerivedPointerObjectId) {
    LLVMContext Ctx;
    Module M("runtime_pointer", Ctx);
    IRBuilder<> Builder(Ctx);
    auto* I32 = Type::getInt32Ty(Ctx);
    auto* I32Ptr = PointerType::getUnqual(I32);
    auto* FT = FunctionType::get(
        Type::getVoidTy(Ctx), {PointerType::getUnqual(I32Ptr)}, false);
    Function* F = Function::Create(
        FT, Function::ExternalLinkage, "runtime_pointer", &M);
    Argument* Handle = &*F->arg_begin();
    Handle->setName("handle");
    Builder.SetInsertPoint(BasicBlock::Create(Ctx, "entry", F));
    LoadInst* Loaded = Builder.CreateLoad(I32Ptr, Handle, "loaded");
    Builder.CreateRetVoid();

    NameMap names;
    names[Handle] = "handle";

    EXPECT_TRUE(getObjectId(Loaded, *F, names).empty());
}

TEST(AccessMetadataBuilder, UsesArgFallbackForUnnamedParamObjectIds) {
    LLVMContext Ctx;
    Module M("metadata", Ctx);
    IRBuilder<> Builder(Ctx);
    auto* I32 = Type::getInt32Ty(Ctx);
    auto* FT = FunctionType::get(Type::getVoidTy(Ctx), {PointerType::getUnqual(I32)}, false);
    Function* F = Function::Create(FT, Function::ExternalLinkage, "foo", &M);
    BasicBlock* BB = BasicBlock::Create(Ctx, "entry", F);
    Builder.SetInsertPoint(BB);
    Builder.CreateRetVoid();

    NameMap names;
    AccessMetadata metadata = buildAccessMetadata(M);

    auto param = metadata.objects.find("function:foo::param:arg0");
    ASSERT_NE(param, metadata.objects.end());
    EXPECT_EQ(param->second.name, "arg0");
    EXPECT_EQ(getObjectId(&*F->arg_begin(), *F, names), "function:foo::param:arg0");
}

TEST(AccessMetadataJson, EmitsParseableObjectTypeInfo) {
    ObjectMetadata object;
    object.id = "global::A";
    object.name = "A";
    object.scope = "global";
    object.storage = "global";
    object.kind = "array";
    object.shape = {100};
    object.elem_type = "i32";
    object.elem_size = 4;
    object.llvm_type = "[100 x i32]";

    llvm::json::Object obj = toJson(object);

    EXPECT_FALSE(obj.getString("type").hasValue());
    EXPECT_EQ(str(obj.getString("kind")), "array");
    auto* shape = obj.getArray("shape");
    ASSERT_NE(shape, nullptr);
    ASSERT_EQ(shape->size(), 1u);
    EXPECT_EQ(i64((*shape)[0].getAsInteger()), 100);
    EXPECT_EQ(str(obj.getString("elem_type")), "i32");
    EXPECT_EQ(i64(obj.getInteger("elem_size")), 4);
    EXPECT_EQ(str(obj.getString("llvm_type")), "[100 x i32]");
}

TEST(AccessMetadataJson, EmitsParseableFieldTypeInfo) {
    FieldMetadata field;
    field.name = "items";
    field.index = 1;
    field.offset = 8;
    field.size = 64;
    field.kind = "array";
    field.shape = {4};
    field.elem_type = "S";
    field.elem_size = 16;
    field.llvm_type = "[4 x %struct.S]";

    llvm::json::Object obj = toJson(field);

    EXPECT_FALSE(obj.getString("type").hasValue());
    EXPECT_EQ(str(obj.getString("kind")), "array");
    EXPECT_EQ(str(obj.getString("elem_type")), "S");
    EXPECT_EQ(i64(obj.getInteger("elem_size")), 16);
    EXPECT_EQ(str(obj.getString("llvm_type")), "[4 x %struct.S]");
}
