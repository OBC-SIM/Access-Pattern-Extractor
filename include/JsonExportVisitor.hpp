#pragma once

#include "Statement.hpp"
#include "llvm/Support/JSON.h"

namespace lat {

/**
 * @brief Loop Annotated Tree를 llvm::json::Value로 직렬화하는 Visitor.
 *
 * 사용 예:
 *   JsonExportVisitor v;
 *   root.accept(v);
 *   llvm::json::Value result = v.getResult();
 */
class JsonExportVisitor : public Visitor {
public:
    const llvm::json::Value& getResult() const { return Result_; }

    void visit(ScalarAccess& node) override {
        llvm::json::Object obj{
            {"type", "Scalar"},
            {"name", node.getName()}
        };
        if (!node.getObjectId().empty())
            obj["object"] = node.getObjectId();
        if (!node.getOp().empty())
            obj["op"] = node.getOp();
        Result_ = std::move(obj);
    }

    void visit(ArrayAccess& node) override {
        llvm::json::Array indices;
        for (const auto& idx : node.getIndexVars())
            indices.push_back(idx);
        llvm::json::Object obj{
            {"type",    "Array"},
            {"name",    node.getArrayName()},
            {"indices", std::move(indices)}
        };
        if (!node.getObjectId().empty())
            obj["object"] = node.getObjectId();
        if (!node.getAccessPath().empty())
            obj["access_path"] = accessPathJson(node.getAccessPath());
        if (!node.getOp().empty())
            obj["op"] = node.getOp();
        Result_ = std::move(obj);
    }

    void visit(CallStmt& node) override {
        llvm::json::Array args;
        for (const auto& arg : node.getArgs())
            args.push_back(arg);
        llvm::json::Array argObjects;
        for (const auto& arg : node.getArgObjects())
            argObjects.push_back(arg);
        Result_ = llvm::json::Object{
            {"type",        "Call"},
            {"callee",      node.getCallee()},
            {"args",        std::move(args)},
            {"arg_objects", std::move(argObjects)}
        };
    }

    void visit(LoopNest& node) override {
        llvm::json::Array body;
        for (const auto& child : node.getBody()) {
            child->accept(*this);
            body.push_back(getResult());
        }
        Result_ = llvm::json::Object{
            {"type",  "Loop"},
            {"var",   node.getInductionVar()},
            {"start", node.getStart()},
            {"bound", node.getBound()},
            {"step",  node.getStep()},
            {"depth", static_cast<int64_t>(node.getDepth())},
            {"body",  std::move(body)}
        };
    }

private:
    static llvm::json::Array accessPathJson(
        const std::vector<AccessPathSegment>& path) {
        llvm::json::Array result;
        for (const auto& segment : path) {
            llvm::json::Object obj{{"kind", segment.kind}};
            if (segment.kind == "field") {
                obj["name"] = segment.name;
                obj["index"] = segment.index;
            } else if (segment.kind == "index") {
                obj["value"] = segment.value;
            }
            result.push_back(std::move(obj));
        }
        return result;
    }

    llvm::json::Value Result_ = nullptr;
};

}  // namespace lat
