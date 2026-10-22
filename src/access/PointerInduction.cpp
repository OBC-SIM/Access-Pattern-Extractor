#include "PointerInduction.hpp"
#include "AccessOperands.hpp"

#include "../index/AffineResolution.hpp"
#include "AccessMetadataBuilder.hpp"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

using namespace llvm;

namespace map
{
namespace
{

class NestedPointerRewriter : public SCEVRewriteVisitor<NestedPointerRewriter>
{
  SmallPtrSet<const PHINode *, 4> active;

public:
  explicit NestedPointerRewriter(ScalarEvolution & evolution)
    : SCEVRewriteVisitor(evolution)
  {
  }

  const SCEV * visitUnknown(const SCEVUnknown * expression)
  {
    auto * phi = dyn_cast<PHINode>(expression->getValue());
    if (!phi || !phi->getType()->isPointerTy() ||
        phi->getNumIncomingValues() != 2)
      return expression;
    // Defense against cyclic SCEV dependencies outside accepted loop nesting.
    if (!active.insert(phi).second)
      index::rejectAffine("cyclic nested pointer recurrence");
    for (unsigned incoming = 0; incoming != 2; ++incoming)
    {
      auto * carried = dyn_cast<PHINode>(phi->getIncomingValue(incoming));
      const auto * recurrence =
        carried ? dyn_cast<SCEVAddRecExpr>(SE.getSCEV(carried)) : nullptr;
      if (!recurrence || !recurrence->isAffine() ||
          recurrence->getStart() != expression)
        continue;
      auto * child = const_cast<Loop *>(recurrence->getLoop());
      auto * owner = child->getParentLoop();
      if (!owner || owner->getHeader() != phi->getParent() ||
          child->getHeader() != carried->getParent() ||
          owner->getExitingBlock() != owner->getHeader() ||
          child->getExitingBlock() != child->getHeader() ||
          !child->getLoopPreheader() || !child->getLoopLatch() ||
          !owner->getLoopPreheader() || !owner->getLoopLatch() ||
          phi->getIncomingBlock(incoming) != owner->getLoopLatch() ||
          phi->getIncomingBlock(1 - incoming) != owner->getLoopPreheader())
        continue;
      if (carried->getNumIncomingValues() != 2 ||
          carried->getIncomingValueForBlock(child->getLoopPreheader()) != phi)
        continue;
      const auto * increment = dyn_cast_or_null<GEPOperator>(
        carried->getIncomingValueForBlock(child->getLoopLatch()));
      if (!increment || increment->getPointerOperand() != carried ||
          increment->getNumIndices() != 1 ||
          !increment->hasAllConstantIndices())
        continue;
      const auto induction = index::resolveInduction(child, SE);
      index::resolveInduction(owner, SE);
      const auto * stride =
        dyn_cast<SCEVConstant>(recurrence->getStepRecurrence(SE));
      const auto * backedges =
        dyn_cast<SCEVConstant>(SE.getBackedgeTakenCount(child));
      if (!stride || !backedges || !isa<SCEVConstant>(induction.start) ||
          !isa<SCEVConstant>(induction.bound))
        index::rejectAffine(
          "nested pointer carry needs an exact constant trip count");
      const auto count = region::tripCount(induction.bounds);
      // Independent SCEV cross-check: canonical eligible fixtures agree here;
      // retain the guard if later changes broaden the accepted loop shapes.
      if (index::affineWide(index::affineInteger(backedges->getAPInt())) !=
          count)
        index::rejectAffine("nested pointer carry has an unproved exit count");
      const auto delta =
        index::affineWide(index::affineInteger(stride->getAPInt())) * count;
      const auto indexWidth =
        phi->getModule()->getDataLayout().getIndexTypeSizeInBits(
          phi->getType());
      if (!delta.isSignedIntN(indexWidth))
        index::rejectAffine("nested pointer carry offset wraps");
      // A header-tested child exits with its PHI advanced count times. This
      // direct PHI use also requires the child header to dominate the latch;
      // a conditional bypass must merge values and cannot match this shape.
      const auto * start =
        visit(SE.getSCEV(phi->getIncomingValue(1 - incoming)));
      const auto * step =
        SE.getConstant(stride->getType(), index::affineInteger(delta), true);
      active.erase(phi);
      return SE.getAddRecExpr(start, step, owner, SCEV::FlagAnyWrap);
    }
    active.erase(phi);
    return expression;
  }
};

}  // namespace

std::unique_ptr<Statement> makePointerInductionAccess(
  Value * pointer, Instruction & instruction, ScalarEvolution & evolution,
  const NameMap & names, const AccessMetadata & metadata,
  const Function & current, const std::string & operation)
{
  auto * root = accessPointerRoot(pointer).value;
  if (!isa<PHINode>(root)) return nullptr;

  const auto * expression =
    NestedPointerRewriter(evolution).visit(evolution.getSCEV(pointer));
  const auto * base =
    dyn_cast<SCEVUnknown>(evolution.getPointerBase(expression));
  auto * object = base ? base->getValue() : nullptr;
  if (object && isa<CallBase, LoadInst>(object))
  {
    // A unique runtime base has no ELF identity. Preserve only recurrences
    // whose offset proof succeeds; legacy extraction emits an unresolved node,
    // while strict extraction rejects the remaining PHI object.
    index::resolveAffine(evolution.removePointerBase(expression), evolution,
                          names, &instruction);
    return nullptr;
  }
  if (!object || !isa<GlobalVariable, Argument, AllocaInst>(object))
    index::rejectAffine("pointer recurrence has no unique storage object");
  const auto objectId = getObjectId(object, current, names);
  const auto entry = metadata.objects.find(objectId);
  if (entry == metadata.objects.end())
    index::rejectAffine("pointer recurrence storage is not registered");

  // Flatten only homogeneous arrays. A struct field offset or a type-punning
  // pointer needs a different layout contract than one scalar element index.
  auto * element = object->getType()->getPointerElementType();
  while (auto * array = dyn_cast<ArrayType>(element))
    element = array->getElementType();
  auto * accessed = accessedType(instruction);
  if ((!element->isIntegerTy() && !element->isFloatingPointTy()) ||
      element != accessed)
    index::rejectAffine("unsupported pointer recurrence element layout");

  auto form = index::resolveAffine(evolution.removePointerBase(expression),
                                   evolution, names, &instruction);
  const auto width = static_cast<int64_t>(
    instruction.getModule()->getDataLayout().getTypeAllocSize(element));
  if (instruction.getModule()->getDataLayout().getTypeStoreSize(accessed) !=
      static_cast<uint64_t>(width))
    index::rejectAffine("array stride differs from access width");
  if (width <= 0 || form.constant % width != 0)
    index::rejectAffine("pointer offset is not an integral element index");
  form.constant /= width;
  for (auto & term : form.terms)
  {
    if (term.second.coefficient % width != 0)
      index::rejectAffine("pointer stride is not an integral element index");
    term.second.coefficient /= width;
  }
  const auto subscript = index::formatAffine(form);
  ArrayMetadata layout;
  layout.elem_size = width;
  auto result = std::make_unique<ArrayAccess>(
    entry->second.name + "[" + subscript + "]",
    std::vector<std::string>{subscript}, layout, operation);
  result->setObjectId(objectId);
  AccessPathSegment segment;
  segment.kind = "index";
  segment.value = subscript;
  result->setAccessPath({segment});
  return result;
}

}  // namespace map
