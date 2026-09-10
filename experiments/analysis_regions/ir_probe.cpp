#include "region_capture.hpp"

#include <stdexcept>
#include <string>

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

namespace {

void inspect(llvm::Module& module) {
    for (auto& function : module) {
        if (function.isDeclaration())
            continue;
        llvm::outs() << "function " << function.getName() << '\n';
        for (auto& instruction : llvm::instructions(function)) {
            llvm::Value* pointer = nullptr;
            if (auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction))
                pointer = load->getPointerOperand();
            if (auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction))
                pointer = store->getPointerOperand();
            if (!pointer)
                continue;
            const auto* object =
                llvm::dyn_cast<llvm::GlobalVariable>(llvm::getUnderlyingObject(pointer));
            if (!object)
                continue;
            llvm::outs() << instruction.getOpcodeName() << ' ' << object->getName() << ' '
                         << (instruction.getMetadata("yarda.region") ? "region" : "outside")
                         << '\n';
        }
    }
}

} // namespace

/**
 * @brief Inspect or capture experimental IR supplied by the fixture runner.
 * @param argc Number of command-line arguments.
 * @param argv Borrowed, non-null command-line argument array.
 * @return Zero on success, nonzero on invalid input or failure.
 */
int main(int argc, char** argv) {
    llvm::llvm_shutdown_obj shutdown;
    if (argc != 3 && argc != 4) {
        llvm::errs() << "usage: region_ir_probe inspect INPUT | capture INPUT OUTPUT\n";
        return 2;
    }
    llvm::LLVMContext context;
    llvm::SMDiagnostic diagnostic;
    auto module = llvm::parseIRFile(argv[2], diagnostic, context);
    if (!module) {
        diagnostic.print(argv[0], llvm::errs());
        return 1;
    }
    try {
        const std::string mode = argv[1];
        if (mode == "inspect" && argc == 3) {
            inspect(*module);
        } else if (mode == "capture" && argc == 4) {
            region_probe::capture(*module);
            if (llvm::verifyModule(*module, &llvm::errs()))
                return 1;
            std::error_code error;
            llvm::raw_fd_ostream output(argv[3], error, llvm::sys::fs::OF_Text);
            if (error)
                throw std::runtime_error(error.message());
            module->print(output, nullptr);
            output.flush();
            if (output.has_error()) {
                output.clear_error();
                throw std::runtime_error("cannot write captured IR");
            }
        } else {
            throw std::invalid_argument("invalid probe invocation");
        }
    } catch (const std::exception& error) {
        llvm::errs() << error.what() << '\n';
        return 1;
    }
}
