#include "AnalysisRegion.hpp"

#include <stdexcept>

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include "region/RegionInstructions.hpp"

using namespace llvm;

namespace map
{
namespace
{

int markerKind(const Instruction & instruction)
{
  const auto * intrinsic = dyn_cast<IntrinsicInst>(&instruction);
  if (!intrinsic || intrinsic->getIntrinsicID() != Intrinsic::annotation)
    return 0;
  StringRef text;
  if (!getConstantStringInfo(intrinsic->getArgOperand(1), text)) return 0;
  if (text == "yarda.region.begin.v1") return 1;
  if (text == "yarda.region.end.v1") return -1;
  return 0;
}

bool site(const Instruction & instruction)
{
  if (region::isNonAccessIntrinsic(instruction)) return false;
  return isa<CallBase>(instruction) || instruction.mayReadOrWriteMemory();
}

bool promoted(const Instruction & instruction)
{
  const Value * pointer = nullptr;
  if (const auto * load = dyn_cast<LoadInst>(&instruction))
    pointer = load->getPointerOperand();
  if (const auto * store = dyn_cast<StoreInst>(&instruction))
    pointer = store->getPointerOperand();
  const auto * allocation = dyn_cast_or_null<AllocaInst>(pointer);
  return allocation &&
         allocation->getParent() ==
           &allocation->getFunction()->getEntryBlock() &&
         isAllocaPromotable(allocation);
}

AnalysisRegion capture(Function & function, Instruction * begin,
                       Instruction * end)
{
  if (!begin || !end || !begin->use_empty() || !end->use_empty())
    throw std::invalid_argument(
      "expected one unused begin/end pair per function");
  DominatorTree dominators(function);
  PostDominatorTree postdominators(function);
  LoopInfo loops(dominators);
  if (!dominators.isReachableFromEntry(begin->getParent()) ||
      !dominators.dominates(begin, end) ||
      !postdominators.dominates(end, begin) ||
      !postdominators.dominates(begin->getParent(), &function.getEntryBlock()))
    throw std::invalid_argument(
      "region requires ordered unavoidable boundaries");
  if (loops.getLoopFor(begin->getParent()) ||
      loops.getLoopFor(end->getParent()))
    throw std::invalid_argument("region boundaries must be outside loops");

  AnalysisRegion result;
  auto * identity = MDNode::get(
    function.getContext(), MDString::get(function.getContext(), "APE_ANALYZE"));
  for (auto & instruction : instructions(function))
  {
    if (&instruction == begin || &instruction == end ||
        !dominators.isReachableFromEntry(instruction.getParent()) ||
        !dominators.dominates(begin, &instruction) ||
        !postdominators.dominates(end, &instruction))
      continue;
    instruction.setMetadata("yarda.region", identity);
    if (site(instruction) && !promoted(instruction))
      result.retainedSites.emplace_back(&instruction);
  }
  for (auto * loop : loops.getLoopsInPreorder())
  {
    if (!loop->getHeader()->getTerminator()->getMetadata("yarda.region"))
      continue;
    for (auto * block : loop->blocks())
      if (dominators.isReachableFromEntry(block) &&
          !block->getTerminator()->getMetadata("yarda.region"))
        throw std::invalid_argument("region must contain complete loops");
    result.loopHeaders.emplace_back(loop->getHeader());
  }
  function.addFnAttr("yarda.region", "APE_ANALYZE");
  begin->eraseFromParent();
  end->eraseFromParent();
  return result;
}

}  // namespace

AnalysisRegions captureAnalysisRegions(Module & module,
                                       const std::set<std::string> & expected)
{
  std::map<std::string, std::pair<Instruction *, Instruction *>> pairs;
  for (auto & function : module)
  {
    for (auto & instruction : instructions(function))
    {
      const auto kind = markerKind(instruction);
      if (!kind) continue;
      auto & pair = pairs[function.getName().str()];
      auto *& slot = kind == 1 ? pair.first : pair.second;
      if (slot) throw std::invalid_argument("duplicate region boundary");
      slot = &instruction;
    }
  }
  if (pairs.size() != expected.size())
    throw std::invalid_argument("source/IR region manifest mismatch");
  for (const auto & name : expected)
    if (!pairs.count(name) || !pairs.at(name).first || !pairs.at(name).second)
      throw std::invalid_argument("source/IR region pair missing: " + name);
  AnalysisRegions result;
  for (const auto & [name, pair] : pairs)
    result.emplace(name,
                   capture(*module.getFunction(name), pair.first, pair.second));
  return result;
}

void validateAnalysisRegions(Module & module, const AnalysisRegions & regions,
                             FunctionAnalysisManager & analyses)
{
  for (const auto & [name, region] : regions)
  {
    auto * function = module.getFunction(name);
    if (!function ||
        function->getFnAttribute("yarda.region").getValueAsString() !=
          "APE_ANALYZE")
      throw std::invalid_argument("region function descriptor lost: " + name);
    auto & loops = analyses.getResult<LoopAnalysis>(*function);
    std::set<const BasicBlock *> headers;
    for (const auto & handle : region.loopHeaders)
    {
      const auto * header = dyn_cast_or_null<BasicBlock>(handle);
      auto * loop = header ? loops.getLoopFor(header) : nullptr;
      if (!loop || loop->getHeader() != header ||
          header->getParent() != function)
        throw std::invalid_argument("selected loop header lost or changed");
      headers.insert(header);
    }
    for (const auto * header : headers)
      for (auto * loop = loops.getLoopFor(header); loop;
           loop = loop->getParentLoop())
        if (!headers.count(loop->getHeader()))
          throw std::invalid_argument("selected loop gained an enclosing loop");
    std::set<const Instruction *> retained;
    for (const auto & handle : region.retainedSites)
    {
      const auto * instruction = dyn_cast_or_null<Instruction>(handle);
      if (!instruction || instruction->getFunction() != function ||
          !site(*instruction) || !instruction->getMetadata("yarda.region"))
        throw std::invalid_argument("selected access site lost or changed");
      retained.insert(instruction);
    }
    for (const auto & instruction : instructions(*function))
    {
      const auto * loop = loops.getLoopFor(instruction.getParent());
      const bool selected = instruction.getMetadata("yarda.region") ||
                            (loop && headers.count(loop->getHeader()));
      if (site(instruction) && selected && !retained.count(&instruction))
        throw std::invalid_argument("additional selected access site");
    }
    for (auto * loop : loops.getLoopsInPreorder())
      if ((loop->getHeader()->getTerminator()->getMetadata("yarda.region") ||
           (loop->getParentLoop() &&
            headers.count(loop->getParentLoop()->getHeader()))) &&
          !headers.count(loop->getHeader()))
        throw std::invalid_argument("additional selected loop header");
  }
}

bool hasRegionTransport(const Module & module)
{
  for (const auto & function : module)
  {
    if (function.hasFnAttribute("yarda.region")) return true;
    for (const auto & instruction : instructions(function))
      if (markerKind(instruction) || instruction.getMetadata("yarda.region"))
        return true;
  }
  return false;
}

}  // namespace map
