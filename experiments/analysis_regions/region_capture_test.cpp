#include "helpers/region_test_support.hpp"

#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ManagedStatic.h"

namespace region_probe::test {

TEST_F(RegionCaptureTest, SelectsOnlyInstructionsBetweenBoundaries) {
    auto module = parse(std::string("store i32 1, i32* @before\n") + beginCall +
                        "store i32 2, i32* @inside\n" + endCall + "store i32 3, i32* @after\n");
    ASSERT_NE(module, nullptr);
    region_probe::capture(*module);
    unsigned tagged = 0;
    unsigned calls = 0;
    for (const auto& instruction : llvm::instructions(module->getFunction("fixture"))) {
        if (llvm::isa<llvm::CallInst>(instruction))
            ++calls;
        if (!instruction.getMetadata("yarda.region"))
            continue;
        ++tagged;
        const auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction);
        ASSERT_NE(store, nullptr);
        EXPECT_EQ(store->getPointerOperand()->getName(), "inside");
    }
    EXPECT_EQ(tagged, 1u);
    EXPECT_EQ(calls, 0u);
}

TEST_F(RegionCaptureTest, SelectsCompleteLoopWithBackEdgeBetweenBoundaries) {
    auto module = parse(std::string("store i32 1, i32* @before\n") + beginCall + R"(
br label %header
header:
%i = phi i32 [0, %entry], [%next, %latch]
%more = icmp slt i32 %i, 3
br i1 %more, label %body, label %finish
body:
%value = load i32, i32* @inside
store i32 %value, i32* @inside
br label %latch
latch:
%next = add i32 %i, 1
br label %header
finish:
)" + endCall + "store i32 3, i32* @after\n");
    ASSERT_NE(module, nullptr);
    region_probe::capture(*module);
    unsigned selectedAccesses = 0;
    for (const auto& instruction : llvm::instructions(module->getFunction("fixture"))) {
        const bool selected = instruction.getMetadata("yarda.region") != nullptr;
        const auto block = instruction.getParent()->getName();
        if (block == "header" || block == "body" || block == "latch") {
            EXPECT_TRUE(selected) << "unselected loop instruction: " << instruction.getOpcodeName();
        }
        if (const auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
            EXPECT_EQ(selected, store->getPointerOperand()->getName() == "inside");
            selectedAccesses += selected;
        }
        if (llvm::isa<llvm::LoadInst>(instruction)) {
            EXPECT_TRUE(selected);
            selectedAccesses += selected;
        }
        EXPECT_FALSE(llvm::isa<llvm::CallInst>(instruction));
    }
    EXPECT_EQ(selectedAccesses, 2u);
}

TEST_F(RegionCaptureTest, KeepsIdentityWhenSelectedBodyIsEmpty) {
    auto module = parse(std::string(beginCall) + endCall);
    ASSERT_NE(module, nullptr);
    region_probe::capture(*module);
    ASSERT_TRUE(module->getFunction("fixture")->hasFnAttribute("yarda.region"));
    EXPECT_EQ(module->getFunction("fixture")->getFnAttribute("yarda.region").getValueAsString(),
              "APE_ANALYZE");
    for (const auto& instruction : llvm::instructions(module->getFunction("fixture"))) {
        EXPECT_EQ(instruction.getMetadata("yarda.region"), nullptr);
        EXPECT_FALSE(llvm::isa<llvm::CallInst>(instruction));
    }
}

TEST_F(RegionCaptureTest, ExcludesUnreachableInstructionsLeadingToRegionEnd) {
    auto module = parse(std::string("store i32 1, i32* @before\n") + beginCall +
                        "store i32 2, i32* @inside\nbr label %finish\n"
                        "dead:\nstore i32 9, i32* @after\nbr label %finish\nfinish:\n" +
                        endCall + "store i32 3, i32* @after\n");
    ASSERT_NE(module, nullptr);
    region_probe::capture(*module);
    unsigned selectedStores = 0;
    for (const auto& instruction : llvm::instructions(module->getFunction("fixture"))) {
        const bool selected = instruction.getMetadata("yarda.region") != nullptr;
        if (instruction.getParent()->getName() == "dead") {
            EXPECT_FALSE(selected);
        }
        if (const auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
            EXPECT_EQ(selected, store->getPointerOperand()->getName() == "inside");
            selectedStores += selected;
        }
    }
    EXPECT_EQ(selectedStores, 1u);
}

TEST_F(RegionCaptureTest, RejectsMissingEnd) {
    auto module = parse(beginCall);
    ASSERT_NE(module, nullptr);
    expectRejection(*module, "expected exactly one begin/end region pair");
}

TEST_F(RegionCaptureTest, RejectsMissingBegin) {
    auto module = parse(endCall);
    ASSERT_NE(module, nullptr);
    expectRejection(*module, "expected exactly one begin/end region pair");
}

TEST_F(RegionCaptureTest, RejectsReversedBoundaries) {
    auto module = parse(std::string(endCall) + beginCall);
    ASSERT_NE(module, nullptr);
    expectRejection(*module, "region requires ordered unavoidable boundaries");
}

TEST_F(RegionCaptureTest, RejectsDuplicateBoundaries) {
    auto module = parse(std::string(beginCall) + beginCall + endCall + endCall);
    ASSERT_NE(module, nullptr);
    expectRejection(*module, "expected exactly one begin/end region pair");
}

TEST_F(RegionCaptureTest, RejectsLostExpectedRegion) {
    auto module = parse("store i32 1, i32* @inside\n");
    ASSERT_NE(module, nullptr);
    expectRejection(*module, "expected exactly one begin/end region pair");
}

TEST_F(RegionCaptureTest, RejectsPathThatBypassesEnd) {
    auto module =
        parse(std::string(beginCall) +
              "br i1 true, label %exit, label %finish\nexit:\nret void\nfinish:\n" + endCall);
    ASSERT_NE(module, nullptr);
    expectRejection(*module, "region requires ordered unavoidable boundaries");
}

TEST_F(RegionCaptureTest, RejectsBoundariesInDifferentFunctions) {
    auto module = parseFunctions(std::string("define void @first() {\nentry:\n") + beginCall +
                                 "ret void\n}\ndefine void @second() {\nentry:\n" + endCall +
                                 "ret void\n}\n");
    ASSERT_NE(module, nullptr);
    expectRejection(*module, "region boundaries cross functions");
}

TEST_F(RegionCaptureTest, RejectsUsedMarkerResults) {
    for (const bool useBegin : {true, false}) {
        SCOPED_TRACE(useBegin ? "begin result" : "end result");
        const auto body = useBegin ? std::string("%marker =") + beginCall + endCall
                                   : std::string(beginCall) + "%marker =" + endCall;
        auto module = parse(body + "store i32 %marker, i32* @inside\n");
        ASSERT_NE(module, nullptr);
        expectRejection(*module, "region marker result must be unused");
    }
}

TEST_F(RegionCaptureTest, RejectsBoundariesInsideLoop) {
    auto module = parse(std::string("br label %loop\nloop:\n") +
                        "%i = phi i32 [0, %entry], [%next, %loop]\n" + beginCall +
                        "store i32 %i, i32* @inside\n" + endCall +
                        "%next = add i32 %i, 1\n%more = icmp slt i32 %next, 3\n"
                        "br i1 %more, label %loop, label %finish\nfinish:\n");
    ASSERT_NE(module, nullptr);
    expectRejection(*module, "region boundaries must be outside loops");
}

TEST_F(RegionCaptureTest, RejectsUnreachableBegin) {
    auto module = parse(std::string("ret void\ndead:\n") + beginCall + endCall);
    ASSERT_NE(module, nullptr);
    expectRejection(*module, "region requires ordered unavoidable boundaries");
}

} // namespace region_probe::test

/**
 * @brief Run boundary specifications and release LLVM process registries.
 * @param argc Number of command-line arguments.
 * @param argv Borrowed, non-null command-line argument array.
 * @return The GTest exit status.
 */
int main(int argc, char** argv) {
    llvm::llvm_shutdown_obj shutdown;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
