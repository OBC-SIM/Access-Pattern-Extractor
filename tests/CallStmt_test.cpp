#include <gtest/gtest.h>
#include "llvm/Support/JSON.h"

#include "../include/JsonExportVisitor.hpp"
#include "../include/Statement.hpp"

using namespace lat;

// json::Value는 lvalue에서 getAsObject()를 호출해야 한다.
static const llvm::json::Object* toObj(const llvm::json::Value& V) {
    return V.getAsObject();
}

static std::string str(llvm::Optional<llvm::StringRef> opt) {
    return opt ? opt->str() : "";
}

TEST(CallStmt, Getters) {
    CallStmt call("helper", {"arr", "i"}, {"global::arr", "i"});
    EXPECT_EQ(call.getCallee(), "helper");
    ASSERT_EQ(call.getArgs().size(), 2u);
    EXPECT_EQ(call.getArgs()[0], "arr");
    EXPECT_EQ(call.getArgs()[1], "i");
    ASSERT_EQ(call.getArgObjects().size(), 2u);
    EXPECT_EQ(call.getArgObjects()[0], "global::arr");
    EXPECT_EQ(call.getArgObjects()[1], "i");
}

TEST(CallStmt, JsonFields) {
    CallStmt call("helper", {"arr", "i"}, {"global::arr", "i"});
    JsonExportVisitor vis;
    call.accept(vis);
    auto* obj = toObj(vis.getResult());
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(str(obj->getString("type")), "Call");
    EXPECT_EQ(str(obj->getString("callee")), "helper");
    auto* args = obj->getArray("args");
    ASSERT_NE(args, nullptr);
    ASSERT_EQ(args->size(), 2u);
    EXPECT_EQ(str((*args)[0].getAsString()), "arr");
    EXPECT_EQ(str((*args)[1].getAsString()), "i");
    auto* argObjects = obj->getArray("arg_objects");
    ASSERT_NE(argObjects, nullptr);
    ASSERT_EQ(argObjects->size(), 2u);
    EXPECT_EQ(str((*argObjects)[0].getAsString()), "global::arr");
    EXPECT_EQ(str((*argObjects)[1].getAsString()), "i");
}
