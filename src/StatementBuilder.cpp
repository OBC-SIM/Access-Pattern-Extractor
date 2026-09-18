#include "StatementBuilder.hpp"

#include <map>
#include <stdexcept>

#include "AccessBuilder.hpp"
#include "index/LoopInduction.hpp"
#include "llvm/ADT/PostOrderIterator.h"
#include "region/RegionAccessBuilder.hpp"
#include "region/RegionLoopBounds.hpp"

using namespace llvm;

namespace map
{

static std::unique_ptr<map::LoopNest> buildLoopNest(
  Loop * L, ScalarEvolution & SE, unsigned depth, const NameMap & names,
  const std::set<const Function *> & inlineFuncs,
  const map::AccessMetadata & metadata, const Function & current, bool strict);

/**
 * @brief 루프 바디의 직접 BB에서 메모리 접근 Statement를 수집한다.
 *
 * 서브루프 BB는 건너뛰고, 서브루프 헤더를 만나면 자식 LoopNest로 삽입한다.
 *
 * @param L     현재 루프
 * @param SE    ScalarEvolution 분석 결과
 * @param depth 현재 루프 깊이
 * @param nest  Statement를 추가할 대상 LoopNest
 * @param names llvm.dbg.value 기반 Value → 변수명 맵
 */
static void populateBody(Loop * L, ScalarEvolution & SE, unsigned depth,
                         map::LoopNest & nest, const NameMap & names,
                         const std::set<const Function *> & inlineFuncs,
                         const map::AccessMetadata & metadata,
                         const Function & current, bool strict)
{
  std::set<BasicBlock *> subLoopBlocks;
  std::map<BasicBlock *, Loop *> subLoopHeaders;
  for (Loop * Sub : L->getSubLoops())
  {
    subLoopHeaders[Sub->getHeader()] = Sub;
    for (BasicBlock * BB : Sub->blocks()) subLoopBlocks.insert(BB);
  }

  std::set<Loop *> processed;
  std::vector<BasicBlock *> ordered;
  if (strict)
  {
    for (auto * block :
         ReversePostOrderTraversal<Function *>(L->getHeader()->getParent()))
      if (L->contains(block)) ordered.push_back(block);
  }
  else
  {
    ordered.assign(L->block_begin(), L->block_end());
  }
  for (BasicBlock * BB : ordered)
  {
    auto hIt = subLoopHeaders.find(BB);
    if (hIt != subLoopHeaders.end())
    {
      if (!processed.count(hIt->second))
      {
        processed.insert(hIt->second);
        nest.addChild(buildLoopNest(hIt->second, SE, depth + 1, names,
                                    inlineFuncs, metadata, current, strict));
      }
      continue;
    }
    if (subLoopBlocks.count(BB)) continue;

    for (Instruction & I : *BB)
      if (auto stmt = strict ? region::makeRegionAccess(I, SE, names, metadata,
                                                        inlineFuncs, current)
                             : makeAccessFromInstr(I, SE, names, metadata,
                                                   inlineFuncs, current))
        nest.addChild(std::move(stmt));
  }
}

static std::unique_ptr<map::LoopNest> buildLoopNest(
  Loop * L, ScalarEvolution & SE, unsigned depth, const NameMap & names,
  const std::set<const Function *> & inlineFuncs,
  const map::AccessMetadata & metadata, const Function & current, bool strict)
{
  const auto bounds = strict ? region::resolveLoopBounds(*L, SE)
                             : index::resolveInduction(L, SE).bounds;
  auto nest = std::make_unique<map::LoopNest>(getInductionVarName(L, SE, names),
                                              bounds.start, bounds.bound, depth,
                                              bounds.step);
  populateBody(L, SE, depth, *nest, names, inlineFuncs, metadata, current,
               strict);
  return nest;
}

// ── 루트 Statement 빌더 ───────────────────────────────────

/**
 * @brief 함수 전체 BB를 RPO(Reverse Post-Order)로 순회해 최상위 Statement
 * 목록을 구성한다.
 *
 * RPO는 CFG 흐름 순서를 따르므로 루프 전/후 코드가 올바른 순서로 출력된다.
 * 최상위 루프 헤더 BB → LoopNest, 루프 외부 BB → Scalar/Array 노드 삽입.
 * 루프 내부 BB는 buildLoopNest가 처리하므로 건너뛴다.
 *
 * @param F     분석 대상 함수
 * @param LI    LoopInfo 분석 결과
 * @param SE    ScalarEvolution 분석 결과
 * @param names llvm.dbg.value 기반 Value → 변수명 맵
 * @param root  결과를 추가할 최상위 Statement 벡터
 */
void buildRootStatements(Function & F, LoopInfo & LI, ScalarEvolution & SE,
                         const NameMap & names,
                         const std::set<const Function *> & inlineFuncs,
                         const map::AccessMetadata & metadata,
                         std::vector<std::unique_ptr<Statement>> & root,
                         const AnalysisRegion * selection, bool strict)
{
  std::map<BasicBlock *, Loop *> topLoopHeaders;
  std::set<BasicBlock *> topLoopBlocks;
  std::set<const BasicBlock *> selectedHeaders;
  if (selection)
    for (const auto & header : selection->loopHeaders)
    {
      const auto * block = dyn_cast_or_null<BasicBlock>(header);
      if (!block || block->getParent() != &F)
        throw std::invalid_argument("selected loop header lost or changed");
      selectedHeaders.insert(block);
    }
  for (Loop * L : LI)
  {
    topLoopHeaders[L->getHeader()] = L;
    for (BasicBlock * BB : L->blocks()) topLoopBlocks.insert(BB);
  }

  std::set<Loop *> processed;
  for (BasicBlock * BB : llvm::ReversePostOrderTraversal<Function *>(&F))
  {
    auto hIt = topLoopHeaders.find(BB);
    if (hIt != topLoopHeaders.end())
    {
      if (!processed.count(hIt->second))
      {
        processed.insert(hIt->second);
        if (!selection || selectedHeaders.count(BB))
          root.push_back(buildLoopNest(hIt->second, SE, 1, names, inlineFuncs,
                                       metadata, F, strict));
      }
      continue;
    }
    if (topLoopBlocks.count(BB)) continue;

    for (Instruction & I : *BB)
    {
      if (selection && !I.getMetadata("yarda.region")) continue;
      if (auto stmt =
            strict
              ? region::makeRegionAccess(I, SE, names, metadata, inlineFuncs, F)
              : makeAccessFromInstr(I, SE, names, metadata, inlineFuncs, F))
        root.push_back(std::move(stmt));
    }
  }
}

}  // namespace map
