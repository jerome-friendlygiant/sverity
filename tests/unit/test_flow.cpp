// Flow-sensitive statement analysis (src/analysis/flow.h): reaching writes, definite assignment, kills, guards.
#include <memory>

#include "analysis/flow.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/syntax/SyntaxTree.h"
#include "testing.h"

using namespace slang;
using namespace slang::ast;
using namespace sverity::flow;

namespace {
struct Compiled {
    std::unique_ptr<Compilation> comp;
    std::vector<const ProceduralBlockSymbol*> procs;
};
struct Collect : ASTVisitor<Collect, VisitFlags::AllGood> {
    std::vector<const ProceduralBlockSymbol*> v;
    void handle(const ProceduralBlockSymbol& p) { v.push_back(&p); }
};
Compiled compile(const std::string& text) {
    Compiled c;
    c.comp = std::make_unique<Compilation>();
    c.comp->addSyntaxTree(syntax::SyntaxTree::fromText(text));
    (void)c.comp->getAllDiagnostics();
    Collect col;
    c.comp->getRoot().visit(col);
    c.procs = col.v;
    return c;
}
size_t countReads(const Flow& f, const char* name) {
    size_t n = 0;
    for (auto& a : f.accesses) n += !a.write && a.sym->name == name;
    return n;
}
} // namespace

TEST(flow_latch_and_definite_assignment) {
    auto c = compile(R"(module m(input logic a, b, output logic x, y);
        always @* begin if (a) x = b; if (a) y = 1; else y = 0; end endmodule)");
    CHECK_EQ(c.procs.size(), 1u);
    Flow f(*c.procs[0]);
    for (auto* s : f.writtenSymbols()) {
        bool isX = s->name == "x";
        CHECK_EQ(f.unassignedAtEnd(*s).empty(), !isX);   // x is not assigned on the path where a is false
    }
}

TEST(flow_partial_bits_and_case_default) {
    auto c = compile(R"(module m(input logic [1:0] s, output logic [3:0] x);
        always_comb begin x[1:0] = 2'd1; case (s) 0: x[3:2] = 0; 1: x[3:2] = 1; default: x[3:2] = 2; endcase end endmodule)");
    Flow f(*c.procs[0]);
    CHECK(f.unassignedAtEnd(*f.writtenSymbols()[0]).empty());
    auto c2 = compile(R"(module m(input logic [1:0] s, output logic [3:0] x);
        always_comb begin x[1:0] = 2'd1; case (s) 0: x[3:2] = 0; 1: x[3:2] = 1; endcase end endmodule)");
    Flow f2(*c2.procs[0]);
    auto gaps = f2.unassignedAtEnd(*f2.writtenSymbols()[0]);
    CHECK_EQ(gaps.size(), 1u);
    if (gaps.size() == 1) { CHECK_EQ(gaps[0].first, 2u); CHECK_EQ(gaps[0].second, 3u); }
}

TEST(flow_read_before_write_and_reaching) {
    auto c = compile(R"(module m(input logic a, output logic x, y);
        always_comb begin y = x; x = a; end endmodule)");
    Flow f(*c.procs[0]);
    bool seen = false;
    for (auto& a : f.accesses)
        if (!a.write && a.sym->name == "x") { seen = true; CHECK(a.entryOnly); CHECK(a.reaching.empty()); }
    CHECK(seen);
    auto c2 = compile(R"(module m(input logic a, output logic x, y);
        always_comb begin x = a; y = x; end endmodule)");
    Flow f2(*c2.procs[0]);
    for (auto& a : f2.accesses)
        if (!a.write && a.sym->name == "x") { CHECK(!a.entryMaybe); CHECK_EQ(a.reaching.size(), 1u); }
}

TEST(flow_dead_store_and_default_idiom) {
    auto c = compile(R"(module m(input logic a, b, output logic x);
        always_comb begin x = 1; x = a; end endmodule)");
    Flow f(*c.procs[0]);
    CHECK_EQ(f.kills.size(), 1u);
    if (!f.kills.empty()) CHECK(!f.kills[0].readBefore);
    auto c2 = compile(R"(module m(input logic a, b, output logic x);
        always_comb begin x = 0; if (a) x = b; end endmodule)");
    Flow f2(*c2.procs[0]);
    // the default write is overwritten on one path only; it is still live (reaches the end) on the other
    bool liveDefault = false;
    for (auto& a : f2.accesses) if (a.write && a.guards->empty()) liveDefault = a.live;
    CHECK(liveDefault);
}

TEST(flow_guards_exclusive_and_override) {
    auto c = compile(R"(module m(input logic [1:0] s, input logic a, b, output logic x, y);
        always_comb begin x = 0; y = 0;
          if (s == 1) x = a; else if (s == 2) x = b;
          if (a) y = 1; if (b) y = 0; end endmodule)");
    Flow f(*c.procs[0]);
    const Access *xa = nullptr, *xb = nullptr, *ya = nullptr, *yb = nullptr;
    for (auto& a : f.accesses) {
        if (!a.write || a.guards->empty()) continue;
        if (a.sym->name == "x") (xa ? xb : xa) = &a;
        else (ya ? yb : ya) = &a;
    }
    CHECK(xa && xb && ya && yb);
    if (xa && xb) CHECK(f.exclusive(*xa, *xb));
    if (ya && yb) CHECK(!f.exclusive(*ya, *yb));
}

TEST(flow_nonblocking_reads_see_old_value) {
    auto c = compile(R"(module m(input logic clk, d, output logic q, r);
        always_ff @(posedge clk) begin q <= d; r <= q; end endmodule)");
    Flow f(*c.procs[0]);
    for (auto& a : f.accesses)
        if (!a.write && a.sym->name == "q") { CHECK(a.entryOnly); CHECK(a.reaching.empty()); }
    CHECK_EQ(countReads(f, "q"), 1u);
}

TEST(flow_timing_control_exposes_writes) {
    auto c = compile(R"(module m(input logic a, b, output logic q);
        initial begin q = a; #1; q = b; end endmodule)");
    Flow f(*c.procs[0]);
    CHECK(f.opaque);
    CHECK_EQ(f.kills.size(), 1u);
    if (!f.kills.empty()) CHECK(f.kills[0].observed);
}
