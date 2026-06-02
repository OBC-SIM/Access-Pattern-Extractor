#include "../include/AccessBuilder.hpp"

#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"

#include "../include/AccessPath.hpp"
#include "../include/AccessMetadataBuilder.hpp"

using namespace llvm;

namespace lat {

static bool isStorageObject(Value* V) {
    if (!V->getType()->isPointerTy())
        return false;
    Value* Base = V->stripPointerCasts();
    return isa<Argument>(Base) || isa<GlobalVariable>(Base) || isa<AllocaInst>(Base);
}

static std::unique_ptr<Statement> makeInlineCall(
    CallBase& Call,
    const NameMap& names,
    const std::set<const Function*>& inlineFuncs,
    const Function& current) {
    Function* Callee = Call.getCalledFunction();
    if (!Callee || Callee == &current || !inlineFuncs.count(Callee))
        return nullptr;

    std::vector<std::string> args;
    std::vector<std::string> argObjects;
    for (Value* Arg : Call.args()) {
        args.push_back(getValueName(Arg, names));
        if (isStorageObject(Arg))
            argObjects.push_back(getObjectId(Arg, current, names));
        else
            argObjects.push_back(getValueName(Arg, names));
    }
    return std::make_unique<CallStmt>(
        Callee->getName().str(), args, std::move(argObjects));
}

static Value* loadStorePointer(Instruction& I, std::string& op) {
    if (auto* Load = dyn_cast<LoadInst>(&I)) {
        op = "load";
        return Load->getPointerOperand();
    }
    if (auto* Store = dyn_cast<StoreInst>(&I)) {
        op = "store";
        return Store->getPointerOperand();
    }
    return nullptr;
}

static std::unique_ptr<Statement> makeArrayOrScalar(
    Value* ptr,
    Instruction& I,
    ScalarEvolution& SE,
    const NameMap& names,
    const AccessMetadata& metadata,
    const Function& current,
    std::string op) {
    if (auto* GEP = dyn_cast<GEPOperator>(ptr)) {
        auto desc = describeGepAccess(GEP, SE, names, metadata);
        std::string base = std::move(desc.name);
        auto indices = std::move(desc.indices);
        if (indices.empty())
            return std::make_unique<ScalarAccess>(base, std::move(op));
        auto access = std::make_unique<ArrayAccess>(
            base,
            indices,
            getArrayMetadata(GEP, I.getModule()->getDataLayout()),
            std::move(op));
        access->setObjectId(getObjectId(GEP->getPointerOperand(), current, names));
        access->setAccessPath(std::move(desc.access_path));
        return access;
    }

    Value* base = ptr->stripPointerCasts();
    if (!isa<Argument>(base) && !isa<GlobalVariable>(base) && !isa<AllocaInst>(base))
        return nullptr;

    std::string name = getBaseName(ptr, names);
    for (const User* U : base->users()) {
        if (isa<GetElementPtrInst>(U)) {
            auto access = std::make_unique<ArrayAccess>(
                name, std::vector<std::string>{"0"}, std::move(op));
            access->setObjectId(getObjectId(ptr, current, names));
            return access;
        }
    }
    return std::make_unique<ScalarAccess>(name, std::move(op));
}

std::unique_ptr<Statement> makeAccessFromInstr(
    Instruction& I,
    ScalarEvolution& SE,
    const NameMap& names,
    const AccessMetadata& metadata,
    const std::set<const Function*>& inlineFuncs,
    const Function& current) {
    if (auto* Call = dyn_cast<CallBase>(&I))
        return makeInlineCall(*Call, names, inlineFuncs, current);

    std::string op;
    Value* ptr = loadStorePointer(I, op);
    if (!ptr)
        return nullptr;

    return makeArrayOrScalar(ptr, I, SE, names, metadata, current, std::move(op));
}

}  // namespace lat
