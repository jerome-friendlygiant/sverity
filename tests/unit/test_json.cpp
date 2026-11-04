#include "sverity/json.h"
#include "testing.h"
using namespace sverity::json;

TEST(json_roundtrip) {
    auto r = parse(R"({"a":[1,2.5,"x\né",true,null],"b":{"c":-7}})");
    CHECK(r.ok);
    CHECK_EQ(r.value["a"].asArray().size(), 5u);
    CHECK_EQ(r.value["a"].asArray()[2].asString(), std::string("x\n\xc3\xa9"));
    CHECK_EQ(r.value["b"]["c"].asInt(), -7);
    auto again = parse(r.value.dump(-1));
    CHECK(again.ok);
    CHECK_EQ(again.value.dump(-1), r.value.dump(-1));
}
TEST(json_errors) {
    CHECK(!parse("{").ok);
    CHECK(!parse("[1,]").ok);
    CHECK(!parse("{\"a\":1} x").ok);
    CHECK(!parse("\"abc").ok);
    CHECK(!parse("").ok);
    CHECK(!parse("{\"a\":1,\"a\":2}").ok);
}
TEST(json_object_order_is_insertion_order) {
    Value v = Value::object();
    v.set("z", 1).set("a", 2).set("z", 3);
    CHECK_EQ(v.dump(-1), std::string("{\"z\":3,\"a\":2}"));
}

TEST(json_output_is_always_valid_utf8) {
    std::string bad = "ok\xED\xA0\x80 \xC3\x28 \xFF end";   // surrogate, bad continuation, invalid byte
    std::string esc = escape(bad);
    CHECK(esc.find("\xED\xA0\x80") == std::string::npos);
    CHECK(esc.find("\xEF\xBF\xBD") != std::string::npos);
    CHECK(parse("\"\\ud800\"").ok == false);        // lone surrogate escape rejected
    CHECK(parse("\"\\ud83d\\ude00\"").ok);          // valid pair accepted
    CHECK_EQ(escape("h\xC3\xA9llo"), std::string("h\xC3\xA9llo"));
}
