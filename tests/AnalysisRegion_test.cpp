#include <gtest/gtest.h>

#include "MapModuleBuilder.hpp"
#include "helpers/RegionIrFixture.hpp"

namespace
{

using map::test::RegionIr;

TEST(AnalysisRegion, MatchesManifestAndRetainsOnlyEnclosedSitesWithoutDebugInfo)
{
  RegionIr fixture;
  EXPECT_TRUE(map::hasRegionTransport(*fixture.module));
  auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
  ASSERT_EQ(regions.size(), 1U);
  EXPECT_EQ(regions.at("kernel").loopHeaders.size(), 1U);
  EXPECT_EQ(regions.at("kernel").retainedSites.size(), 2U);
  auto * load =
    llvm::dyn_cast<llvm::LoadInst>(regions.at("kernel").retainedSites[0]);
  auto * store =
    llvm::dyn_cast<llvm::StoreInst>(regions.at("kernel").retainedSites[1]);
  ASSERT_NE(load, nullptr);
  ASSERT_NE(store, nullptr);
  EXPECT_EQ(load->getPointerOperand(), fixture.module->getGlobalVariable("a"));
  EXPECT_EQ(store->getPointerOperand(), fixture.module->getGlobalVariable("a"));
  EXPECT_NO_THROW(
    map::validateAnalysisRegions(*fixture.module, regions, fixture.functions));
}

TEST(AnalysisRegion, RejectsExtraMissingAndCrossFunctionManifestIdentities)
{
  for (const auto & expected :
       {std::set<std::string>{}, {"other"}, {"kernel", "other"}})
  {
    RegionIr fixture;
    EXPECT_THROW(map::captureAnalysisRegions(*fixture.module, expected),
                 std::invalid_argument);
  }
  RegionIr fixture;
  map::captureAnalysisRegions(*fixture.module, {"kernel"});
  EXPECT_THROW(map::captureAnalysisRegions(*fixture.module, {"kernel"}),
               std::invalid_argument);
}

TEST(AnalysisRegion, RejectsLostOrUntaggedSelectedSite)
{
  for (const bool erase : {false, true})
  {
    RegionIr fixture;
    auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
    auto * store =
      llvm::cast<llvm::Instruction>(regions.at("kernel").retainedSites[1]);
    if (erase)
      store->eraseFromParent();
    else
      store->setMetadata("yarda.region", nullptr);
    EXPECT_THROW(
      map::validateAnalysisRegions(*fixture.module, regions, fixture.functions),
      std::invalid_argument);
  }
}

TEST(AnalysisRegion, RejectsAdditionalTaggedMemorySite)
{
  RegionIr fixture;
  auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
  auto * instruction =
    llvm::cast<llvm::Instruction>(regions.at("kernel").retainedSites[0]);
  auto * clone = instruction->clone();
  clone->insertBefore(instruction);
  EXPECT_THROW(
    map::validateAnalysisRegions(*fixture.module, regions, fixture.functions),
    std::invalid_argument);
}

TEST(AnalysisRegion, RejectsReplacementOfOriginalSiteEvenIfTagsAreCopied)
{
  RegionIr fixture;
  auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
  auto * instruction =
    llvm::cast<llvm::Instruction>(regions.at("kernel").retainedSites[0]);
  auto * clone = instruction->clone();
  clone->insertBefore(instruction);
  instruction->replaceAllUsesWith(clone);
  instruction->eraseFromParent();
  EXPECT_THROW(
    map::validateAnalysisRegions(*fixture.module, regions, fixture.functions),
    std::invalid_argument);
}

TEST(AnalysisRegion, RejectsAdditionalUntaggedMemorySiteInsideSelectedLoop)
{
  RegionIr fixture;
  auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
  auto * original =
    llvm::cast<llvm::Instruction>(regions.at("kernel").retainedSites[0]);
  auto * extra = original->clone();
  extra->setMetadata("yarda.region", nullptr);
  extra->insertBefore(original);
  EXPECT_THROW(
    map::validateAnalysisRegions(*fixture.module, regions, fixture.functions),
    std::invalid_argument);
}

TEST(AnalysisRegion, RejectsAdditionalUntaggedCallInsideSelectedLoop)
{
  RegionIr fixture;
  auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
  auto * original =
    llvm::cast<llvm::Instruction>(regions.at("kernel").retainedSites[0]);
  llvm::IRBuilder<> builder(original);
  auto callee =
    fixture.module->getOrInsertFunction("external", builder.getVoidTy());
  builder.CreateCall(callee);
  EXPECT_THROW(
    map::validateAnalysisRegions(*fixture.module, regions, fixture.functions),
    std::invalid_argument);
}

TEST(AnalysisRegion, KeepsUntaggedOutsideMemoryOutOfSelection)
{
  RegionIr fixture;
  auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
  auto * function = fixture.module->getFunction("kernel");
  llvm::IRBuilder<> builder(&function->getEntryBlock().front());
  builder.CreateLoad(builder.getInt32Ty(),
                     fixture.module->getGlobalVariable("a"));
  EXPECT_NO_THROW(
    map::validateAnalysisRegions(*fixture.module, regions, fixture.functions));
  auto result = map::buildMapModule(*fixture.module, fixture.modules, &regions);
  const auto & body =
    *(*result.getArray("functions"))[0].getAsObject()->getArray("body");
  ASSERT_EQ(body.size(), 1U);
  EXPECT_EQ(body[0].getAsObject()->getArray("body")->size(), 2U);
}

TEST(AnalysisRegion, RejectsLostLoopHeaderIdentity)
{
  RegionIr fixture;
  auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
  regions.at("kernel").loopHeaders[0] = nullptr;
  EXPECT_THROW(
    map::validateAnalysisRegions(*fixture.module, regions, fixture.functions),
    std::invalid_argument);
}

TEST(AnalysisRegion, BuilderRejectsLostHeaderInsteadOfAsserting)
{
  RegionIr fixture;
  auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
  regions.at("kernel").loopHeaders[0] = nullptr;
  EXPECT_THROW(map::buildMapModule(*fixture.module, fixture.modules, &regions),
               std::invalid_argument);
}

TEST(AnalysisRegion, DoesNotRequireTagsOnScalarPhiOrControlInstructions)
{
  RegionIr fixture;
  auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
  for (auto & instruction :
       llvm::instructions(*fixture.module->getFunction("kernel")))
    if (llvm::isa<llvm::PHINode, llvm::BranchInst>(instruction))
      instruction.setMetadata("yarda.region", nullptr);
  EXPECT_NO_THROW(
    map::validateAnalysisRegions(*fixture.module, regions, fixture.functions));
  const auto result =
    map::buildMapModule(*fixture.module, fixture.modules, &regions);
  const auto * body =
    (*result.getArray("functions"))[0].getAsObject()->getArray("body");
  ASSERT_EQ(body->size(), 1U);
  EXPECT_EQ(*(*body)[0].getAsObject()->getString("type"), "Loop");
}

TEST(AnalysisRegion, EmptySelectionDependsOnDescriptorAfterMarkersAreErased)
{
  RegionIr fixture(R"(
@begin = constant [22 x i8] c"yarda.region.begin.v1\00"
@end = constant [20 x i8] c"yarda.region.end.v1\00"
declare i32 @llvm.annotation.i32(i32, i8*, i8*, i32)
define void @kernel() {
  %b = call i32 @llvm.annotation.i32(i32 0, i8* getelementptr
    ([22 x i8], [22 x i8]* @begin, i32 0, i32 0), i8* null, i32 1)
  %e = call i32 @llvm.annotation.i32(i32 0, i8* getelementptr
    ([20 x i8], [20 x i8]* @end, i32 0, i32 0), i8* null, i32 2)
  ret void
}
)");
  auto regions = map::captureAnalysisRegions(*fixture.module, {"kernel"});
  EXPECT_TRUE(regions.at("kernel").retainedSites.empty());
  EXPECT_TRUE(regions.at("kernel").loopHeaders.empty());
  auto * function = fixture.module->getFunction("kernel");
  EXPECT_TRUE(function->hasFnAttribute("yarda.region"));
  for (const auto & instruction : llvm::instructions(*function))
    EXPECT_FALSE(llvm::isa<llvm::CallBase>(instruction));
  EXPECT_NO_THROW(
    map::validateAnalysisRegions(*fixture.module, regions, fixture.functions));
  function->removeFnAttr("yarda.region");
  EXPECT_FALSE(map::hasRegionTransport(*fixture.module));
  EXPECT_THROW(
    map::validateAnalysisRegions(*fixture.module, regions, fixture.functions),
    std::invalid_argument);
}

}  // namespace
