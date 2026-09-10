#include "region_capture.hpp"

#include <stdexcept>
#include <vector>

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"

namespace region_probe {

void capture(llvm::Module& module) {
    std::vector<llvm::IntrinsicInst*> begins;
    std::vector<llvm::IntrinsicInst*> ends;
    for (auto& function : module) {
        for (auto& instruction : llvm::instructions(function)) {
            auto* intrinsic = llvm::dyn_cast<llvm::IntrinsicInst>(&instruction);
            if (!intrinsic || intrinsic->getIntrinsicID() != llvm::Intrinsic::annotation)
                continue;
            llvm::StringRef text;
            if (!llvm::getConstantStringInfo(intrinsic->getArgOperand(1), text))
                continue;
            if (text == "yarda.region.begin.v1")
                begins.push_back(intrinsic);
            if (text == "yarda.region.end.v1")
                ends.push_back(intrinsic);
        }
    }
    if (begins.size() != 1 || ends.size() != 1)
        throw std::invalid_argument("expected exactly one begin/end region pair");
    auto* begin = begins.front();
    auto* end = ends.front();
    if (begin->getFunction() != end->getFunction())
        throw std::invalid_argument("region boundaries cross functions");
    if (!begin->use_empty() || !end->use_empty())
        throw std::invalid_argument("region marker result must be unused");
    auto& function = *begin->getFunction();
    llvm::DominatorTree dominators(function);
    llvm::PostDominatorTree postdominators(function);
    llvm::LoopInfo loops(dominators);
    if (!dominators.isReachableFromEntry(begin->getParent()) || !dominators.dominates(begin, end) ||
        !postdominators.dominates(end, begin))
        throw std::invalid_argument("region requires ordered unavoidable boundaries");
    if (loops.getLoopFor(begin->getParent()) || loops.getLoopFor(end->getParent()))
        throw std::invalid_argument("region boundaries must be outside loops");

    auto* identity = llvm::MDNode::get(module.getContext(),
                                       llvm::MDString::get(module.getContext(), "APE_ANALYZE"));
    for (auto& instruction : llvm::instructions(function)) {
        if (&instruction != begin && &instruction != end &&
            dominators.isReachableFromEntry(instruction.getParent()) &&
            dominators.dominates(begin, &instruction) &&
            postdominators.dominates(end, &instruction))
            instruction.setMetadata("yarda.region", identity);
    }
    function.addFnAttr("yarda.region", "APE_ANALYZE");
    begin->eraseFromParent();
    end->eraseFromParent();
}

} // namespace region_probe
