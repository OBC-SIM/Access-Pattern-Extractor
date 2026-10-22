#include <gtest/gtest.h>

#include "../../src/region/RegionAccessBuilder.hpp"
#include "../helpers/IndexIrFixture.hpp"
#include "AccessBuilder.hpp"
#include "AccessMetadataBuilder.hpp"
#include "StatementBuilder.hpp"

namespace map::test
{
namespace
{

std::string pointerLoop(const std::string & start = "0",
                        const std::string & step = "1",
                        const std::string & offset = "0")
{
  return "@a = global [16 x i32] zeroinitializer\n"
         "define void @kernel() {\nentry:\n"
         " %base = getelementptr [16 x i32], [16 x i32]* @a, i64 0, i64 " +
         start +
         "\n br label %header\nheader:\n"
         " %i = phi i64 [0, %entry], [%next, %body]\n"
         " %p = phi i32* [%base, %entry], [%nextp, %body]\n"
         " %test = icmp slt i64 %i, 3\n"
         " br i1 %test, label %body, label %exit\nbody:\n"
         " %address = getelementptr i32, i32* %p, i64 " +
         offset +
         "\n %value = load i32, i32* %address\n"
         " %nextp = getelementptr i32, i32* %p, i64 " +
         step +
         "\n %next = add i64 %i, 1\n br label %header\n"
         "exit: ret void\n}\n";
}

std::unique_ptr<Statement> access(IndexIr & ir)
{
  return makeAccessFromInstr(*ir.value("value"), ir.evolution(), {},
                             buildAccessMetadata(*ir.module), {},
                             *ir.module->getFunction("kernel"));
}

void expectAccessRejected(IndexIr & ir, const std::string & reason)
{
  try
  {
    access(ir);
    FAIL() << "unsupported pointer access accepted";
  }
  catch (const std::invalid_argument & error)
  {
    EXPECT_EQ(error.what(), "unsupported affine index: " + reason);
  }
}

std::string nestedPointerLoop(const std::string & bound = "4",
                              const std::string & start = "0",
                              const std::string & step = "1",
                              const std::string & predicate = "slt")
{
  return R"(@a = global [64 x i32] zeroinitializer
define void @kernel(i1 %choose) {
entry:
 %base = getelementptr [64 x i32], [64 x i32]* @a, i64 0, i64 0
 br label %outer
outer:
 %i = phi i64 [0, %entry], [%nexti, %latch]
 %row = phi i32* [%base, %entry], [%p, %latch]
 %outertest = icmp slt i64 %i, 3
 br i1 %outertest, label %pre, label %exit
pre: br label %inner
inner:
 %j = phi i64 [)" +
         start + R"(, %pre], [%nextj, %body]
 %p = phi i32* [%row, %pre], [%nextp, %body]
 %test = icmp )" +
         predicate + " i64 %j, " + bound + R"(
 br i1 %test, label %body, label %latch
body:
 %value = load i32, i32* %p
 %nextp = getelementptr i32, i32* %p, i64 1
 %nextj = add i64 %j, )" +
         step + R"(
 br label %inner
latch:
 %nexti = add i64 %i, 1
 br label %outer
exit: ret void
}
)";
}

TEST(PointerInduction, CarriesExactChildExitPointerIntoOuterLoop)
{
  IndexIr ir(nestedPointerLoop());
  auto statement = access(ir);
  const auto * result = dynamic_cast<ArrayAccess *>(statement.get());
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->getObjectId(), "global::a");
  EXPECT_EQ(result->getIndexVars(), (std::vector<std::string>{"4*i+j"}));
}

TEST(PointerInduction, PreservesInnerInvariantCarriedPointer)
{
  auto source = nestedPointerLoop();
  const std::string load = "load i32, i32* %p";
  source.replace(source.find(load), load.size(), "load i32, i32* %row");
  IndexIr ir(source);
  auto statement = access(ir);
  const auto * result = dynamic_cast<ArrayAccess *>(statement.get());
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->getIndexVars(), (std::vector<std::string>{"4*i"}));
}

TEST(PointerInduction, CarriesDescendingNonunitChildByItsIterationCount)
{
  IndexIr ir(nestedPointerLoop("1", "7", "-2", "sgt"));
  auto statement = access(ir);
  const auto * result = dynamic_cast<ArrayAccess *>(statement.get());
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->getIndexVars(), (std::vector<std::string>{"3*i+j"}));
  auto & function = *ir.module->getFunction("kernel");
  auto & loops = ir.functions.getResult<llvm::LoopAnalysis>(function);
  std::vector<std::unique_ptr<Statement>> statements;
  buildRootStatements(function, loops, ir.evolution(), {}, {},
                      buildAccessMetadata(*ir.module), statements);
  ASSERT_EQ(statements.size(), 1U);
  const auto * outer = dynamic_cast<LoopNest *>(statements[0].get());
  ASSERT_NE(outer, nullptr);
  ASSERT_EQ(outer->getBody().size(), 1U);
  const auto * inner = dynamic_cast<LoopNest *>(outer->getBody()[0].get());
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(inner->getStart(), 0);
  EXPECT_EQ(inner->getBound(), 3);
  EXPECT_EQ(inner->getStep(), 1);
}

TEST(PointerInduction, RejectsUnnormalizedNonunitOuterCarry)
{
  auto source = nestedPointerLoop("3");
  const std::string increment = "%nexti = add i64 %i, 1";
  source.replace(source.find(increment), increment.size(),
                 "%nexti = add i64 %i, 2");
  const std::string bound = "%outertest = icmp slt i64 %i, 3";
  source.replace(source.find(bound), bound.size(),
                 "%outertest = icmp slt i64 %i, 6");
  IndexIr ir(source);
  expectAccessRejected(ir, "pointer stride is not an integral element index");
}

TEST(PointerInduction, ZeroTripChildDoesNotAdvanceOuterPointer)
{
  auto source = nestedPointerLoop("0");
  const std::string load = " %value = load i32, i32* %p\n";
  source.erase(source.find(load), load.size());
  source.insert(source.find(" %nexti ="), " %value = load i32, i32* %row\n");
  IndexIr ir(source);
  auto statement = access(ir);
  const auto * result = dynamic_cast<ArrayAccess *>(statement.get());
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->getIndexVars(), (std::vector<std::string>{"0"}));
}

TEST(PointerInduction, RejectsConditionalChildCarry)
{
  auto source = nestedPointerLoop();
  const std::string carry = "[%p, %latch]";
  source.replace(source.find(carry), carry.size(), "[%selected, %latch]");
  source.insert(source.find(" %nexti ="),
                " %selected = select i1 %choose, i32* %p, i32* %row\n");
  IndexIr ir(source);
  expectAccessRejected(ir, "pointer recurrence has no unique storage object");
}

TEST(PointerInduction, RejectsVariableChildCount)
{
  for (const auto & bound : {"%i", "%limit"})
  {
    auto source = nestedPointerLoop(bound);
    const std::string arguments = "i1 %choose";
    source.replace(source.find(arguments), arguments.size(),
                   "i1 %choose, i64 %limit");
    IndexIr ir(source);
    expectAccessRejected(ir, std::string(bound) == "%i" ? "nested pointer "
                                                          "carry needs an "
                                                          "exact constant trip "
                                                          "count"
                                                        : "loop bound is not "
                                                          "bound to an outer "
                                                          "loop");
  }
}

TEST(PointerInduction, RejectsChildWithEarlyExit)
{
  auto source = nestedPointerLoop();
  source.insert(source.find(" %value ="),
                " br i1 %choose, label %work, label %exit\nwork:\n");
  const std::string incoming = ", %body]";
  for (auto pos = source.find(incoming); pos != std::string::npos;
       pos = source.find(incoming))
    source.replace(pos, incoming.size(), ", %work]");
  IndexIr ir(source);
  expectAccessRejected(ir, "pointer recurrence has no unique storage object");
}

TEST(PointerInduction, RejectsOuterLoopWithEarlyExit)
{
  auto source = nestedPointerLoop();
  const std::string backedge = "br label %outer\nexit:";
  source.replace(source.find(backedge), backedge.size(),
                 "br i1 %choose, label %outer, label %exit\nexit:");
  IndexIr ir(source);
  expectAccessRejected(ir, "pointer recurrence has no unique storage object");
}

TEST(PointerInduction, RejectsOverflowingChildCarry)
{
  auto source = nestedPointerLoop("2");
  source.insert(0, "target datalayout = \"e-p:64:64:64:32\"\n");
  const std::string increment = "getelementptr i32, i32* %p, i64 1";
  source.replace(source.find(increment), increment.size(),
                 "getelementptr i32, i32* %p, i64 268435456");
  IndexIr ir(source);
  // The second body offset is 2^30 (valid i32); only the exit carry 2^31 wraps.
  expectAccessRejected(ir, "nested pointer carry offset wraps");
}

TEST(PointerInduction, PreservesEveryPostincrementAccess)
{
  auto source = pointerLoop();
  const std::string load = "load i32, i32* %address";
  source.replace(source.find(load), load.size(), "load i32, i32* %p");
  IndexIr ir(source);
  auto statement = access(ir);
  const auto * result = dynamic_cast<ArrayAccess *>(statement.get());
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->getObjectId(), "global::a");
  EXPECT_EQ(result->getIndexVars(), (std::vector<std::string>{"i"}));
  EXPECT_EQ(result->getMetadata().elem_size, 4);
  EXPECT_EQ(result->getOp(), "load");
}

TEST(PointerInduction, PreservesOffsetAfterPostincrementPointer)
{
  IndexIr ir(pointerLoop("2", "1", "1"));
  auto statement = access(ir);
  const auto * result = dynamic_cast<ArrayAccess *>(statement.get());
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->getObjectId(), "global::a");
  EXPECT_EQ(result->getIndexVars(), (std::vector<std::string>{"i+3"}));
}

TEST(PointerInduction, PreservesDescendingPointerStride)
{
  IndexIr ir(pointerLoop("6", "-2"));
  auto statement = access(ir);
  const auto * result = dynamic_cast<ArrayAccess *>(statement.get());
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->getObjectId(), "global::a");
  EXPECT_EQ(result->getIndexVars(), (std::vector<std::string>{"-2*i+6"}));
}

TEST(PointerInduction, RejectsNonintegralInitialElementOffset)
{
  auto source = pointerLoop();
  source.insert(source.find(" br label %header"),
                " %bytes = bitcast i32* %base to i8*\n"
                " %byteoffset = getelementptr i8, i8* %bytes, i64 1\n"
                " %initial = bitcast i8* %byteoffset to i32*\n");
  const std::string initial = "[%base, %entry]";
  source.replace(source.find(initial), initial.size(), "[%initial, %entry]");
  IndexIr ir(source);
  expectAccessRejected(ir, "pointer offset is not an integral element index");
}

TEST(PointerInduction, RejectsUnregisteredStorageObject)
{
  IndexIr ir(pointerLoop());
  try
  {
    makeAccessFromInstr(*ir.value("value"), ir.evolution(), {}, {}, {},
                        *ir.module->getFunction("kernel"));
    FAIL() << "unregistered storage accepted";
  }
  catch (const std::invalid_argument & error)
  {
    EXPECT_STREQ(error.what(),
                 "unsupported affine index: "
                 "pointer recurrence storage is not registered");
  }
}

TEST(PointerInduction, RejectsDataDependentPointerStride)
{
  auto source = pointerLoop("0", "%stride");
  source.insert(0, "@stride = global i64 1\n");
  source.insert(source.find(" %base ="), " %stride = load i64, i64* @stride\n");
  IndexIr ir(source);
  expectAccessRejected(ir, "runtime-dependent coefficient");
}

TEST(PointerInduction, RejectsDataSelectedStorageBase)
{
  auto source = pointerLoop();
  source.insert(0, "@b = global [16 x i32] zeroinitializer\n");
  source.replace(source.find("@kernel()"), 9, "@kernel(i1 %choose)");
  source.insert(source.find(" br label %header"),
                " %otherbase = getelementptr [16 x i32], [16 x i32]* @b, i64 "
                "0, i64 0\n"
                " %selected = select i1 %choose, i32* %base, i32* "
                "%otherbase\n");
  const std::string initial = "[%base, %entry]";
  source.replace(source.find(initial), initial.size(), "[%selected, %entry]");
  IndexIr ir(source);
  expectAccessRejected(ir, "pointer recurrence has no unique storage object");
}

TEST(PointerInduction, RejectsRecurrenceThroughAnIncompatibleElementCast)
{
  auto source = pointerLoop();
  source.insert(source.find(" br label %header"),
                " %bytes = bitcast i32* %base to i8*\n");
  for (const auto & replacement :
       {std::make_pair("phi i32* [%base", "phi i8* [%bytes"),
        std::make_pair("getelementptr i32, i32* %p",
                       "getelementptr i8, i8* "
                       "%p"),
        std::make_pair("load i32, i32* %address", "load i8, i8* %address")})
  {
    const std::string old = replacement.first;
    for (auto pos = source.find(old); pos != std::string::npos;
         pos = source.find(old))
      source.replace(pos, old.size(), replacement.second);
  }
  IndexIr ir(source);
  expectAccessRejected(ir, "unsupported pointer recurrence element layout");
}

TEST(PointerInduction, StrictRegionPreservesTheSamePointerRecurrence)
{
  IndexIr ir(pointerLoop("2", "1", "1"));
  const auto metadata = buildAccessMetadata(*ir.module);
  auto statement =
    region::makeRegionAccess(*ir.value("value"), ir.evolution(), {}, metadata,
                             {}, *ir.module->getFunction("kernel"));
  const auto * result = dynamic_cast<ArrayAccess *>(statement.get());
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->getObjectId(), "global::a");
  EXPECT_EQ(result->getIndexVars(), (std::vector<std::string>{"i+3"}));
}

TEST(PointerInduction,
     StrictRegionUsesOneCoordinateForPointerAndIntegerAccesses)
{
  auto source = pointerLoop();
  const std::string increment = "%next = add i64 %i, 1";
  source.replace(source.find(increment), increment.size(),
                 "%next = add i64 %i, 2");
  source.insert(source.find(" %nextp ="),
                " %direct = getelementptr [16 x i32], [16 x i32]* @a, i64 0, "
                "i64 %i\n"
                " %direct_value = load i32, i32* %direct\n");
  IndexIr ir(source);
  auto & function = *ir.module->getFunction("kernel");
  auto & loops = ir.functions.getResult<llvm::LoopAnalysis>(function);
  std::vector<std::unique_ptr<Statement>> statements;
  buildRootStatements(function, loops, ir.evolution(), {}, {},
                      buildAccessMetadata(*ir.module), statements, nullptr,
                      true);
  ASSERT_EQ(statements.size(), 1U);
  const auto * loop = dynamic_cast<LoopNest *>(statements[0].get());
  ASSERT_NE(loop, nullptr);
  EXPECT_EQ(loop->getStart(), 0);
  EXPECT_EQ(loop->getBound(), 2);
  EXPECT_EQ(loop->getStep(), 1);
  ASSERT_EQ(loop->getBody().size(), 2U);
  const auto * pointer = dynamic_cast<ArrayAccess *>(loop->getBody()[0].get());
  const auto * direct = dynamic_cast<ArrayAccess *>(loop->getBody()[1].get());
  ASSERT_NE(pointer, nullptr);
  ASSERT_NE(direct, nullptr);
  EXPECT_EQ(pointer->getIndexVars(), (std::vector<std::string>{"i"}));
  EXPECT_EQ(direct->getIndexVars(), (std::vector<std::string>{"2*i"}));
}

}  // namespace
}  // namespace map::test
