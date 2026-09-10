#pragma once

#include "../region_capture.hpp"

#include <stdexcept>
#include <string>

#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

namespace region_probe::test {

inline constexpr const char* prefix = R"(
@before = global i32 0
@inside = global i32 0
@after = global i32 0
@begin = constant [22 x i8] c"yarda.region.begin.v1\00"
@end = constant [20 x i8] c"yarda.region.end.v1\00"
declare i32 @llvm.annotation.i32(i32, i8*, i8*, i32)
)";

inline constexpr const char* beginCall = R"(
call i32 @llvm.annotation.i32(i32 0, i8* getelementptr ([22 x i8], [22 x i8]* @begin, i32 0, i32 0), i8* null, i32 1)
)";
inline constexpr const char* endCall = R"(
call i32 @llvm.annotation.i32(i32 0, i8* getelementptr ([20 x i8], [20 x i8]* @end, i32 0, i32 0), i8* null, i32 1)
)";

/** @brief Keep boundary specifications independent of LLVM fixture construction. */
class RegionCaptureTest : public testing::Test {
  protected:
    llvm::LLVMContext context;

    std::unique_ptr<llvm::Module> parseFunctions(const std::string& functions) {
        llvm::SMDiagnostic error;
        auto module = llvm::parseAssemblyString(std::string(prefix) + functions, error, context);
        if (!module) {
            error.print("region_capture_tests", llvm::errs());
            return nullptr;
        }
        if (llvm::verifyModule(*module, &llvm::errs()))
            return nullptr;
        return module;
    }

    std::unique_ptr<llvm::Module> parse(const std::string& body) {
        return parseFunctions("define void @fixture() {\nentry:\n" + body + "\nret void\n}\n");
    }

    void expectRejection(llvm::Module& module, const char* reason) {
        try {
            region_probe::capture(module);
            FAIL() << "expected boundary rejection: " << reason;
        } catch (const std::invalid_argument& error) {
            EXPECT_STREQ(error.what(), reason);
        }
    }
};

} // namespace region_probe::test
