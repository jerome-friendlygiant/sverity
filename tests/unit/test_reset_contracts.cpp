// Schema parsing and normalization for the reset_contracts configuration section.
#include <cstdio>
#include <string>

#include "sverity/reset_contracts.h"
#include "sverity/json.h"
#include "testing.h"

using namespace sverity;

namespace {
const char* kExample = R"({
  "sources": [
    {"id":"por", "port":"top.por_n", "active":"low"},
    {"id":"warm", "port":"top.reset_n[3]", "active":"low"},
    {"id":"a", "root":"top.a_rst_n", "active":"low"},
    {"id":"b", "root":"top.b_rst_n", "active":"low"}
  ],
  "assert_together": [{"id":"board_assertion", "sources":["por","warm"]}],
  "release_order": [
    {"id":"board_release", "before":"por", "after":"warm", "clock":"clk_b", "edge":"posedge", "min_cycles":2},
    {"id":"local_release", "before":"a", "after":"b", "clock":"clk_b", "edge":"posedge", "min_cycles":2}
  ],
  "state": [
    {"id":"initial_por", "source":"por", "initial":"asserted"},
    {"id":"initial_warm", "source":"warm", "initial":"asserted"}
  ]
})";

const char* kSource = R"({"id":"por","port":"top.por_n","active":"low"})";
const char* kState = R"({"id":"s","source":"por","initial":"asserted"})";

/// Parses JSON text as a section; false when the text is not JSON or the parser rejects it.
bool parseText(const std::string& text, ResetContracts& c) {
    auto p = json::parse(text);
    std::string error;
    return p.ok && parseResetContracts(p.value, c, error);
}

/// Parses `text` as a section; true when the parser rejects it with a "reset_contracts"-prefixed message.
bool rejects(const char* name, const std::string& text) {
    auto p = json::parse(text);
    if (!p.ok) { std::printf("  fixture %s is not JSON: %s\n", name, p.error.c_str()); return false; }
    ResetContracts out;
    std::string error;
    if (parseResetContracts(p.value, out, error)) { std::printf("  accepted invalid %s\n", name); return false; }
    if (error.rfind("reset_contracts", 0) != 0) { std::printf("  %s: message lacks prefix: %s\n", name, error.c_str()); return false; }
    return true;
}
} // namespace

TEST(reset_contracts_parses_design_example) {
    auto p = json::parse(kExample);
    CHECK(p.ok);
    ResetContracts c;
    std::string error;
    CHECK(parseResetContracts(p.value, c, error));
    CHECK(c.present);
    CHECK_EQ(c.sources.size(), 4u);
    CHECK_EQ(c.sources[0].port, std::string("top.por_n"));
    CHECK(c.sources[0].root.empty());
    CHECK_EQ(c.sources[1].port, std::string("top.reset_n[3]"));
    CHECK_EQ(c.sources[2].root, std::string("top.a_rst_n"));
    CHECK_EQ(c.sources[2].active, std::string("low"));
    CHECK_EQ(c.assertTogether.size(), 1u);
    CHECK_EQ(c.assertTogether[0].sources.size(), 2u);
    CHECK_EQ(c.releaseOrder.size(), 2u);
    CHECK_EQ(c.releaseOrder[0].minCycles, int64_t(2));
    CHECK_EQ(c.releaseOrder[0].edge, std::string("posedge"));
    CHECK_EQ(c.state.size(), 2u);
    CHECK_EQ(c.state[1].initial, std::string("asserted"));
}

TEST(reset_contracts_round_trip_is_normalized) {
    auto p = json::parse(kExample);
    ResetContracts c;
    std::string error;
    CHECK(parseResetContracts(p.value, c, error));
    auto emitted = toJson(c);
    CHECK_EQ(emitted.dump(-1), p.value.dump(-1));
    ResetContracts again;
    CHECK(parseResetContracts(json::parse(emitted.dump(-1)).value, again, error));
    CHECK_EQ(toJson(again).dump(-1), emitted.dump(-1));
}

TEST(reset_contracts_omits_empty_clause_arrays) {
    ResetContracts c;
    std::string error;
    CHECK(parseText(std::string(R"({"sources":[)") + kSource + "],\"state\":[" + kState + "]}", c));
    auto emitted = toJson(c);
    CHECK(emitted.find("sources") != nullptr);
    CHECK(emitted.find("state") != nullptr);
    CHECK(emitted.find("assert_together") == nullptr);
    CHECK(emitted.find("release_order") == nullptr);
}

TEST(reset_contracts_schema_errors) {
    const std::string src = kSource, st = kState;
    const std::string mk = "\"sources\":[" + src + "],\"state\":[" + st + "]";
    auto withSource = [&](const std::string& source) { return "{\"sources\":[" + source + "],\"state\":[" + st + "]}"; };
    auto withState = [&](const std::string& state) { return "{\"sources\":[" + src + "],\"state\":[" + state + "]}"; };
    auto withOrder = [&](const std::string& order) {
        return "{\"sources\":[" + src + ",{\"id\":\"r\",\"root\":\"top.r\",\"active\":\"low\"}],\"release_order\":[" + order + "]}";
    };
    const std::string okOrder = R"({"id":"o","before":"por","after":"r","clock":"clk","edge":"posedge","min_cycles":1})";
    struct Case { const char* name; std::string text; };
    const Case cases[] = {
        {"section not object", "[]"},
        {"unknown top key", "{" + mk.substr(0) + ",\"extra\":1}"},
        {"sources missing", "{\"state\":[" + st + "]}"},
        {"sources empty", "{\"sources\":[],\"state\":[" + st + "]}"},
        {"sources not array", "{\"sources\":{},\"state\":[" + st + "]}"},
        {"no clause", "{\"sources\":[" + src + "]}"},
        {"all clauses empty", "{\"sources\":[" + src + "],\"state\":[],\"assert_together\":[]}"},
        {"source not object", "{\"sources\":[1],\"state\":[" + st + "]}"},
        {"state not object", withState("\"x\"")},
        {"source unknown field", withSource(R"({"id":"por","port":"top.por_n","active":"low","x":1})")},
        {"source missing id", withSource(R"({"port":"top.por_n","active":"low"})")},
        {"source empty id", withSource(R"({"id":"","port":"top.por_n","active":"low"})")},
        {"source missing active", withSource(R"({"id":"por","port":"top.por_n"})")},
        {"source both port and root", withSource(R"({"id":"por","port":"top.p","root":"top.r","active":"low"})")},
        {"source neither port nor root", withSource(R"({"id":"por","active":"low"})")},
        {"source active invalid", withSource(R"({"id":"por","port":"top.p","active":"mid"})")},
        {"source port wildcard", withSource(R"({"id":"por","port":"top.p*","active":"low"})")},
        {"source root brace", withSource(R"({"id":"por","root":"top.{a,b}","active":"low"})")},
        {"source port whitespace", withSource(R"({"id":"por","port":"top. p","active":"low"})")},
        {"duplicate source id", withSource(src + "," + src)},
        {"clause id collides with source", withState(R"({"id":"por","source":"por","initial":"asserted"})")},
        {"state initial invalid", withState(R"({"id":"s","source":"por","initial":"deasserted"})")},
        {"state missing initial", withState(R"({"id":"s","source":"por"})")},
        {"state unknown source", withState(R"({"id":"s","source":"nope","initial":"asserted"})")},
        {"state unknown field", withState(R"({"id":"s","source":"por","initial":"asserted","x":0})")},
        {"group one member", "{\"sources\":[" + src + "],\"assert_together\":[{\"id\":\"g\",\"sources\":[\"por\"]}]}"},
        {"group repeated member", "{\"sources\":[" + src + "],\"assert_together\":[{\"id\":\"g\",\"sources\":[\"por\",\"por\"]}]}"},
        {"group unknown member", "{\"sources\":[" + src + "],\"assert_together\":[{\"id\":\"g\",\"sources\":[\"por\",\"nope\"]}]}"},
        {"group unknown field", "{\"sources\":[" + src + "],\"assert_together\":[{\"id\":\"g\",\"sources\":[\"por\",\"por2\"],\"x\":1}]}"},
        {"order edge invalid", withOrder(R"({"id":"o","before":"por","after":"r","clock":"clk","edge":"both"})")},
        {"order missing clock", withOrder(R"({"id":"o","before":"por","after":"r","edge":"posedge","min_cycles":1})")},
        {"order before equals after", withOrder(R"({"id":"o","before":"por","after":"por","clock":"clk","edge":"posedge","min_cycles":1})")},
        {"order unknown before", withOrder(R"({"id":"o","before":"nope","after":"r","clock":"clk","edge":"posedge","min_cycles":1})")},
        {"order min_cycles missing", withOrder(R"({"id":"o","before":"por","after":"r","clock":"clk","edge":"posedge"})")},
        {"order min_cycles zero", withOrder(R"({"id":"o","before":"por","after":"r","clock":"clk","edge":"posedge","min_cycles":0})")},
        {"order min_cycles overflow", withOrder(R"({"id":"o","before":"por","after":"r","clock":"clk","edge":"posedge","min_cycles":2147483648})")},
        {"order min_cycles fraction", withOrder(R"({"id":"o","before":"por","after":"r","clock":"clk","edge":"posedge","min_cycles":1.5})")},
        {"order min_cycles string", withOrder(R"({"id":"o","before":"por","after":"r","clock":"clk","edge":"posedge","min_cycles":"2"})")},
        {"order unknown field", withOrder(R"({"id":"o","before":"por","after":"r","clock":"clk","edge":"posedge","x":1})")},
        {"duplicate order id", withOrder(okOrder + "," + okOrder)},
    };
    for (const auto& c : cases) CHECK(rejects(c.name, c.text));
}

TEST(reset_contracts_accepts_minimal_valid_sections) {
    ResetContracts c;
    std::string error;
    CHECK(parseText("{\"sources\":[" + std::string(kSource) + "],\"state\":[" + kState + "]}", c));
    CHECK(parseText("{\"sources\":[" + std::string(kSource) + ",{\"id\":\"r\",\"root\":\"top.r\",\"active\":\"high\"}],"
        "\"release_order\":[{\"id\":\"o\",\"before\":\"por\",\"after\":\"r\",\"clock\":\"clk\",\"edge\":\"negedge\",\"min_cycles\":2147483647}]}", c));
    CHECK_EQ(c.releaseOrder[0].minCycles, int64_t(2147483647));
}
