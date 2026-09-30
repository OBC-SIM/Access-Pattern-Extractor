#include "InstructionCounts.hpp"

#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include "IrHelpers.hpp"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;

namespace map
{
namespace
{

std::uint64_t multiply(std::uint64_t left, std::uint64_t right)
{
  if (right && left > std::numeric_limits<std::uint64_t>::max() / right)
    throw std::invalid_argument("IR block execution count exceeds uint64");
  return left * right;
}

class BlockExecutions
{
public:
  LoopInfo & loops;
  ScalarEvolution & evolution;
  std::map<const BasicBlock *, std::uint64_t> counts;

  // Walk one structural iteration. The sole exiting block runs B+1 times;
  // the remaining suffix runs B times, including a pretest loop's body.
  void walk(BasicBlock * block, Loop * loop, std::uint64_t entries,
            std::uint64_t backedges = 0)
  {
    std::set<BasicBlock *> visited;
    auto repetitions = loop ? backedges + 1 : 1;
    bool passedExit = false;
    while (block)
    {
      if (!visited.insert(block).second)
      {
        if (loop && block == loop->getHeader() && passedExit) return;
        throw std::invalid_argument("unsupported cyclic IR control flow");
      }
      if (loop && !loop->contains(block))
        throw std::invalid_argument("unexpected IR loop exit");
      auto * child = loops.getLoopFor(block);
      if (child != loop)
      {
        if (!child || child->getHeader() != block ||
            child->getParentLoop() != loop)
          throw std::invalid_argument("unsupported IR loop entry");
        countLoop(*child, multiply(entries, repetitions));
        block = child->getExitBlock();
        continue;
      }
      counts[block] = multiply(entries, repetitions);
      auto * terminator = block->getTerminator();
      if (isa<ReturnInst>(terminator) && !loop) return;
      const auto * branch = dyn_cast<BranchInst>(terminator);
      if (!branch) throw std::invalid_argument("unsupported IR terminator");
      if (loop && block == loop->getExitingBlock())
      {
        if (!branch->isConditional())
          throw std::invalid_argument("unsupported IR loop exit branch");
        const bool firstInside = loop->contains(branch->getSuccessor(0));
        if (firstInside == loop->contains(branch->getSuccessor(1)))
          throw std::invalid_argument("unsupported IR loop exit successors");
        block = branch->getSuccessor(firstInside ? 0 : 1);
        repetitions = backedges;
        passedExit = true;
      }
      else if (branch->isUnconditional())
        block = branch->getSuccessor(0);
      else if (const auto * constant =
                 dyn_cast<ConstantInt>(branch->getCondition()))
        block = branch->getSuccessor(constant->isZero() ? 1 : 0);
      else
        throw std::invalid_argument("data-dependent IR branch");
    }
    throw std::invalid_argument("IR path has no normal return");
  }

private:
  void countLoop(Loop & loop, std::uint64_t entries)
  {
    if (!loop.getExitingBlock() || !loop.getExitBlock() || !loop.getLoopLatch())
      throw std::invalid_argument("IR loop requires one exit and one latch");
    const auto * count =
      dyn_cast<SCEVConstant>(evolution.getBackedgeTakenCount(&loop));
    if (!count || count->getAPInt().getActiveBits() > 64)
      throw std::invalid_argument("IR loop execution count is not constant");
    const auto backedges = count->getAPInt().getZExtValue();
    if (backedges == std::numeric_limits<std::uint64_t>::max())
      throw std::invalid_argument("IR loop execution count exceeds uint64");
    walk(loop.getHeader(), &loop, entries, backedges);
  }
};

bool excluded(const Instruction & instruction)
{
  const auto * intrinsic = dyn_cast<IntrinsicInst>(&instruction);
  return isa<PHINode>(instruction) ||
         (intrinsic && (isa<DbgInfoIntrinsic>(intrinsic) ||
                        intrinsic->isLifetimeStartOrEnd()));
}

}  // namespace

llvm::json::Object buildInstructionCounts(Function & function,
                                          FunctionAnalysisManager & analyses)
{
  llvm::json::Object result{{"version", 2},
                            {"scope", "function-exclusive"},
                            {"basis", "map-extraction-ir"},
                            {"excluded", "phi-debug-and-lifetime-intrinsics"}};
  try
  {
    auto & loops = analyses.getResult<LoopAnalysis>(function);
    auto & evolution = analyses.getResult<ScalarEvolutionAnalysis>(function);
    BlockExecutions execution{loops, evolution, {}};
    execution.walk(&function.getEntryBlock(), nullptr, 1);
    llvm::json::Array blocks;
    unsigned ordinal = 0;
    for (const auto & block : function)
    {
      std::map<std::string, std::uint64_t> counts;
      llvm::json::Array calls;
      for (const auto & instruction : block)
      {
        if (excluded(instruction)) continue;
        ++counts[instruction.getOpcodeName()];
        if (const auto * call = dyn_cast<CallBase>(&instruction))
        {
          auto * callee =
            dyn_cast<Function>(call->getCalledOperand()->stripPointerCasts());
          const bool expand =
            callee && (hasFunctionAnnotation(*callee, "ape.inline") ||
                       hasFunctionAnnotation(*callee, "yard.inline"));
          calls.push_back(llvm::json::Object{
            {"callee", callee ? llvm::json::Value(callee->getName().str())
                              : llvm::json::Value(nullptr)},
            {"inline", expand}});
        }
      }
      llvm::json::Object opcodes;
      for (const auto & [opcode, count] : counts) opcodes[opcode] = count;
      blocks.push_back(
        llvm::json::Object{{"id", ordinal++},
                           {"name", block.getName().str()},
                           {"executions", execution.counts[&block]},
                           {"opcodes", std::move(opcodes)},
                           {"calls", std::move(calls)}});
    }
    result["status"] = "exact";
    result["blocks"] = std::move(blocks);
  }
  catch (const std::invalid_argument & error)
  {
    result["status"] = "unsupported";
    result["reason"] = std::string(error.what());
  }
  return result;
}

}  // namespace map
