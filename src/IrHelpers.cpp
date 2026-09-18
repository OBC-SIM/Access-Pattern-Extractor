#include "../include/IrHelpers.hpp"

#include <string>
#include <vector>

#include "index/IndexResolution.hpp"
#include "index/LoopInduction.hpp"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace map {

// NameMap·IR hasName 모두 실패 시 최후 식별자.
// Module 컨텍스트를 사용해 슬롯 번호를 포함한 피연산자 표현을 반환한다.
// 무명 로컬 → "3",  전역 변수 → "array",  무명 인자 → "0"
static std::string irOperandName(const Value* V) {
    const Module* M = nullptr;
    if (auto* I = dyn_cast<Instruction>(V))      M = I->getModule();
    else if (auto* A = dyn_cast<Argument>(V))    M = A->getParent()->getParent();
    else if (auto* G = dyn_cast<GlobalValue>(V)) M = G->getParent();

    std::string s;
    raw_string_ostream os(s);
    V->printAsOperand(os, /*PrintType=*/false, M);
    os.flush();
    if (!s.empty() && (s[0] == '%' || s[0] == '@'))
        s = s.substr(1);
    return s;
}

NameMap buildDebugNameMap(Function& F) {
    NameMap names;
    for (BasicBlock& BB : F) {
        for (Instruction& I : BB) {
            if (auto* DVI = dyn_cast<DbgValueInst>(&I))
                if (Value* V = DVI->getValue())
                    if (DILocalVariable* Var = DVI->getVariable())
                        if (!Var->getName().empty())
                            names.emplace(V, Var->getName().str());
            if (auto* DDI = dyn_cast<DbgDeclareInst>(&I))
                if (Value* V = DDI->getAddress())
                    if (DILocalVariable* Var = DDI->getVariable())
                        if (!Var->getName().empty())
                            names.emplace(V->stripPointerCasts(), Var->getName().str());
        }
    }
    return names;
}

std::string getInductionVarName(Loop * L, ScalarEvolution & SE,
                                const NameMap & names)
{
  return index::inductionName(L, SE, names);
}

std::vector<std::string> resolveIndex(Value * Idx, ScalarEvolution & SE,
                                      const NameMap & names)
{
  return {index::resolveSingleIndex(Idx, SE, names)};
}

std::vector<std::string> getIndexVars(GEPOperator * GEP, ScalarEvolution & SE,
                                      const NameMap & names,
                                      const Instruction * useSite)
{
  if (!useSite) useSite = dyn_cast<Instruction>(GEP);
  std::vector<std::string> result;
  if (auto * Parent =
        dyn_cast<GEPOperator>(GEP->getPointerOperand()->stripPointerCasts()))
  {
    auto parentIndices = getIndexVars(Parent, SE, names, useSite);
    result.insert(result.end(), parentIndices.begin(), parentIndices.end());
  }
  auto it = GEP->idx_begin();
  // multi-index GEP의 leading zero만 포인터 역참조로 보고 스킵한다.
  if (GEP->getNumIndices() > 1 && isa<ConstantInt>(*it) &&
      cast<ConstantInt>(*it)->isZero())
    ++it;
  for (; it != GEP->idx_end(); ++it)
    result.push_back(index::resolveSingleIndex(*it, SE, names, useSite));
  return result;
}

std::string getBaseName(Value* Ptr, const NameMap& names) {
    Value* Base = Ptr->stripPointerCasts();
    while (auto* GEP = dyn_cast<GEPOperator>(Base))
        Base = GEP->getPointerOperand()->stripPointerCasts();
    auto it = names.find(Base);
    if (it != names.end()) return it->second;
    if (Base->hasName()) return Base->getName().str();
    if (auto* Arg = dyn_cast<Argument>(Base))
        return "arg" + std::to_string(Arg->getArgNo());
    std::string n = irOperandName(Base);
    return n.empty() ? "arr" : n;
}

std::string getValueName(Value * V, const NameMap & names)
{
  if (auto * C = dyn_cast<ConstantInt>(V))
    return std::to_string(C->getSExtValue());
  if (V->getType()->isPointerTy()) return getBaseName(V, names);

  auto it = names.find(V);
  if (it != names.end()) return it->second;
  if (V->hasName()) return V->getName().str();
  if (auto * Arg = dyn_cast<Argument>(V))
    return "arg" + std::to_string(Arg->getArgNo());
  std::string n = irOperandName(V);
  return n.empty() ? "value" : n;
}

static const GlobalVariable* globalFromStringPointer(const Value* V) {
    V = V->stripPointerCasts();
    if (auto* GV = dyn_cast<GlobalVariable>(V))
        return GV;
    if (auto* CE = dyn_cast<ConstantExpr>(V)) {
        if (CE->getOpcode() == Instruction::GetElementPtr && CE->getNumOperands() > 0)
            return dyn_cast<GlobalVariable>(CE->getOperand(0)->stripPointerCasts());
    }
    return nullptr;
}

static std::string annotationString(const Value* V) {
    const GlobalVariable* GV = globalFromStringPointer(V);
    if (!GV || !GV->hasInitializer())
        return "";
    if (auto* Data = dyn_cast<ConstantDataArray>(GV->getInitializer()))
        if (Data->isCString())
            return Data->getAsCString().str();
    return "";
}

bool hasFunctionAnnotation(Function& F, StringRef Annotation) {
    GlobalVariable* Annos = F.getParent()->getGlobalVariable("llvm.global.annotations");
    if (!Annos || !Annos->hasInitializer())
        return false;

    auto* Entries = dyn_cast<ConstantArray>(Annos->getInitializer());
    if (!Entries)
        return false;

    for (const Use& U : Entries->operands()) {
        auto* Entry = dyn_cast<ConstantStruct>(U.get());
        if (!Entry || Entry->getNumOperands() < 2)
            continue;

        Value* Annotated = Entry->getOperand(0)->stripPointerCasts();
        if (Annotated != &F)
            continue;
        if (annotationString(Entry->getOperand(1)) == Annotation)
            return true;
    }
    return false;
}

}  // namespace map
