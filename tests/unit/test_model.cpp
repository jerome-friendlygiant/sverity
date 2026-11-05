// Shared model: edge operators, reset values per bit, library cell kinds, black-box ports.
#include <filesystem>
#include <fstream>

#include "domains/model.h"
#include "domains/cone.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "sverity/frontend.h"
#include "testing.h"

using namespace sverity;
using namespace sverity::model;

namespace {
struct Loaded {
    Report report;
    std::unique_ptr<Design> design;
    Model* m = nullptr;
};
std::unique_ptr<Loaded> load(const std::string& text, const std::vector<LibCell>& cells = {}) {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() / ("sverity_model_test_" + std::to_string(counter++));
    std::filesystem::create_directories(dir);
    auto file = dir / "t.sv";
    { std::ofstream(file) << text; }
    auto l = std::make_unique<Loaded>();
    FrontendOptions fo;
    fo.slangArgs = {"--top", "top", file.string()};
    l->design = loadDesign(fo, l->report);
    for (auto& c : cells) l->design->libCells[c.name] = c;
    l->m = &getModel(*l->design);
    std::filesystem::remove_all(dir);
    return l;
}
NodeId nodeNamed(const Model& m, const std::string& suffix) {
    for (NodeId i = 0; i < m.nodes.size(); i++) {
        auto n = m.nameOf(i);
        if (n.size() >= suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0) return i;
    }
    return kNoNode;
}
const Edge* edgeBetween(const Model& m, NodeId from, NodeId to) {
    for (auto ei : m.out[from]) if (m.edges[ei].to == to) return &m.edges[ei];
    return nullptr;
}
} // namespace

TEST(model_edges_carry_operator_and_polarity) {
    auto l = load(R"(module top(input logic a_n, b_n, c, d, output logic y, z, w);
        assign y = a_n & b_n;
        assign z = ~(c | d);
        assign w = c ? a_n : b_n;
      endmodule)");
    auto& m = *l->m;
    auto* ay = edgeBetween(m, nodeNamed(m, "top.a_n"), nodeNamed(m, "top.y"));
    CHECK(ay && ay->op == Op::And && ay->inv == 0);
    auto* cz = edgeBetween(m, nodeNamed(m, "top.c"), nodeNamed(m, "top.z"));
    CHECK(cz && cz->op == Op::Or && cz->inv == 1);
    auto* cw = edgeBetween(m, nodeNamed(m, "top.c"), nodeNamed(m, "top.w"));
    CHECK(cw && cw->op == Op::MuxSel);
    auto* aw = edgeBetween(m, nodeNamed(m, "top.a_n"), nodeNamed(m, "top.w"));
    CHECK(aw && aw->op == Op::Mux);
}

TEST(model_ctrl_edges_carry_branch_and_condition_operator) {
    auto l = load(R"(module top(input logic clk, rst_n, clr, d, output logic q);
        always_ff @(posedge clk) if (!rst_n || clr) q <= 1'b0; else q <= d;
      endmodule)");
    auto& m = *l->m;
    NodeId q = nodeNamed(m, "top.q");
    bool sawOr = false, sawNot = false;
    for (auto ei : m.in[q]) {
        const Edge& e = m.edges[ei];
        if (!e.ctrl) continue;
        std::string src = m.nameOf(e.from);
        if (src.ends_with("clr")) sawOr = e.op == Op::Or && e.inv == 0;
        if (src.ends_with("rst_n")) sawNot = e.op == Op::Or && e.inv == 0 || e.op == Op::Not;
    }
    CHECK(sawOr);
    CHECK(sawNot);
}

TEST(model_reset_values_per_bit) {
    auto l = load(R"(module top(input logic clk, rst_n, input logic [3:0] d, output logic [3:0] q);
        always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 4'b10x0; else q <= d;
      endmodule)");
    auto& m = *l->m;
    const char expect[4] = {'0', 'x', '0', '1'};   // bit 0..3 of 4'b10x0
    for (int b = 0; b < 4; b++) {
        NodeId q = nodeNamed(m, "top.q[" + std::to_string(b) + "]");
        auto* f = m.flopAt(q);
        CHECK(f && f->resetValues.size() == 1 && f->resetValues[0] == expect[b]);
    }
}

TEST(model_wide_signal_is_tracked_per_bit) {
    auto l = load(R"(module top(input logic clk, input logic [4095:0] d, output logic [4095:0] q);
        always_ff @(posedge clk) q <= d;
      endmodule)");
    auto& m = *l->m;
    CHECK(!m.collapsed(nodeNamed(m, "top.q[17]")));
    CHECK_EQ(m.flops.size(), 4096u);
}

TEST(model_library_kinds) {
    LibCell flop;
    flop.name = "DFFR"; flop.kind = "flop"; flop.clock = "CLK"; flop.input = "D"; flop.output = "Q"; flop.stages = 1;
    flop.reset = "RN"; flop.resetValue = '1';
    LibCell gate;
    gate.name = "ICG"; gate.kind = "clock_gate"; gate.clock = "CLK"; gate.enable = "EN"; gate.output = "GCLK"; gate.enableLatched = true;
    LibCell ram;
    ram.name = "SPRAM"; ram.kind = "ram"; ram.writeClock = "WCLK"; ram.readClock = "RCLK";
    ram.ports = {{"WCLK", "in", "clock"}, {"RCLK", "in", "clock"}, {"WD", "in", "data_in"}, {"RD", "out", "data_out"}};
    auto l = load(R"(module DFFR(input logic CLK, RN, D, output logic Q); endmodule
      module ICG(input logic CLK, EN, output logic GCLK); endmodule
      module SPRAM(input logic WCLK, RCLK, WD, output logic RD); endmodule
      module top(input logic clk, clk2, rst_n, en, d, output logic q, r, output logic gq);
        logic g;
        DFFR u_f(.CLK(clk), .RN(rst_n), .D(d), .Q(q));
        ICG u_g(.CLK(clk), .EN(en), .GCLK(g));
        always_ff @(posedge g) gq <= d;
        SPRAM u_r(.WCLK(clk), .RCLK(clk2), .WD(d), .RD(r));
      endmodule)", {flop, gate, ram});
    auto& m = *l->m;
    auto* f = m.flopAt(nodeNamed(m, "top.q"));
    CHECK(f && f->resets.size() == 1 && f->resetValues[0] == '1' && f->resets[0].activeLow);
    CHECK_EQ(m.clockGates.size(), 1u);
    CHECK(!m.clockGates.empty() && m.clockGates[0].latched);
    // the gated clock traces back to the ungated clock port: gq is in clk's domain
    auto* gq = m.flopAt(nodeNamed(m, "top.gq"));
    auto* qf = f;
    CHECK(gq && qf && gq->clockResolved && gq->domain == qf->domain);
    CHECK_EQ(m.rams.size(), 1u);
    CHECK(m.blackBoxDriven[nodeNamed(m, "top.r")] == 2);
}

TEST(model_black_box_ports_are_recorded) {
    auto l = load(R"(module top(input logic clk, d, output logic q);
        unknown_ip u(.clk(clk), .din(d), .dout(q));
      endmodule)");
    auto& m = *l->m;
    CHECK_EQ(m.blackBoxes.size(), 1u);
    if (m.blackBoxes.size() == 1) {
        CHECK_EQ(m.blackBoxes[0].ports.size(), 3u);
        CHECK(!m.blackBoxes[0].declared);
        CHECK(m.blackBoxes[0].ports[0].dir == PortDir::Unknown);
    }
}

TEST(model_constant_drivers_keep_x_and_z) {
    auto l = load(R"(module top(input logic clk, output logic [2:0] y, output logic q);
        assign y = 3'b1xz;
        always_ff @(posedge clk) q <= 1'b0;
      endmodule)");
    auto& m = *l->m;
    auto get = [&](const std::string& n) {
        auto it = m.constDrivers.find(nodeNamed(m, n));
        return it == m.constDrivers.end() || it->second.empty() ? '?' : it->second[0].value;
    };
    CHECK_EQ(get("top.y[2]"), '1');
    CHECK_EQ(get("top.y[1]"), 'x');
    CHECK_EQ(get("top.y[0]"), 'z');
    CHECK_EQ(get("top.q"), '0');
}

TEST(model_nested_boolean_expression_is_not_classified_as_one_operator) {
    auto l = load(R"(module top(input logic a, b, c, output logic y);
        assign y = (a & b) | (a & c);
      endmodule)");
    auto& m = *l->m;
    auto* ay = edgeBetween(m, nodeNamed(m, "top.a"), nodeNamed(m, "top.y"));
    CHECK(ay && ay->op == Op::Other && ay->inv == 2);
    auto* by = edgeBetween(m, nodeNamed(m, "top.b"), nodeNamed(m, "top.y"));
    CHECK(by && by->op == Op::Other);
}

TEST(model_port_connection_edges_carry_the_actual_polarity) {
    auto l = load(R"(module child(input logic rst, output logic q); assign q = rst; endmodule
      module top(input logic rst_n, output logic q);
        child u(.rst(~rst_n), .q(q));
      endmodule)");
    auto& m = *l->m;
    auto* e = edgeBetween(m, nodeNamed(m, "top.rst_n"), nodeNamed(m, "top.u.rst"));
    CHECK(e && e->inv == 1);
}

TEST(model_trailing_reset_is_per_bit_and_retains_guard_dependency) {
    auto l = load(R"(module top(input logic clk, rst, input logic [1:0] d, output logic [1:0] q);
        always_ff @(posedge clk) begin q <= d; if (rst) q[0] <= 0; end
      endmodule)");
    auto& m = *l->m;
    auto q0 = nodeNamed(m, "top.q[0]"), q1 = nodeNamed(m, "top.q[1]");
    auto guard = nodeNamed(m, "top.rst");
    auto* f0 = m.flopAt(q0); auto* f1 = m.flopAt(q1);
    CHECK(f0 && f0->hasSyncResetCandidate && f0->syncReset == guard);
    CHECK(f1 && !f1->hasSyncResetCandidate && f1->syncReset == kNoNode);
    auto* e = edgeBetween(m, guard, q0);
    CHECK(e && e->ctrl);
}

TEST(model_constant_false_conjunct_does_not_add_control_edge) {
    auto l = load(R"(module top #(parameter MODE = 0)(input logic clk, en, d, output logic q, p);
        always_ff @(posedge clk) begin
            q <= d;
            if (MODE && en) q <= 0;
            p <= d;
            if (en) p <= 0;
        end
      endmodule)");
    auto& m = *l->m;
    auto en = nodeNamed(m, "top.en");
    CHECK(edgeBetween(m, en, nodeNamed(m, "top.q")) == nullptr);
    auto* live = edgeBetween(m, en, nodeNamed(m, "top.p"));
    CHECK(live && live->ctrl);
}

TEST(model_subroutine_summaries_preserve_hidden_initializer_and_effect_reads) {
    auto l = load(R"(module top(input logic clk, a, output logic q, side, arg_out);
      function automatic logic sample(); logic saved = a; return saved; endfunction
      function automatic logic effect(); side++; return a; endfunction
      task automatic copy_to(output logic v); v = a; endtask
      always @(posedge clk) begin q <= sample() ^ effect(); copy_to(arg_out); end
    endmodule)");
    const auto& m = *l->m;
    auto a = nodeNamed(m, "top.a"), q = nodeNamed(m, "top.q");
    auto side = nodeNamed(m, "top.side"), arg = nodeNamed(m, "top.arg_out");
    CHECK(a != kNoNode && q != kNoNode && side != kNoNode && arg != kNoNode);
    CHECK(edgeBetween(m, a, q));
    CHECK(m.flopAt(arg));
    CHECK(m.flopAt(side));
    CHECK(m.uncertainCallDrivers.count(q));
    CHECK(m.uncertainCallDrivers.count(side));
    CHECK(m.uncertainCallDrivers.count(arg));
}

TEST(model_subroutine_recursion_is_path_specific_even_without_dependency_edges) {
    auto l = load(R"(module top(input logic clk, output logic q);
      function automatic logic recurse(); return recurse(); endfunction
      always @(posedge clk) q <= recurse();
    endmodule)");
    auto q = nodeNamed(*l->m, "top.q");
    CHECK(q != kNoNode);
    CHECK(!l->m->callUncertainties.empty());
    CHECK(l->m->uncertainCallDrivers.count(q));
    CHECK_EQ(l->m->subroutineSummaries, 1u);
}

TEST(model_subroutine_body_is_cached_across_many_calls) {
    std::string text = R"(module top(input logic clk, a, output logic [127:0] q);
      function automatic logic sample(); return a; endfunction
      always @(posedge clk) begin )";
    for (int i = 0; i < 128; ++i) text += "q[" + std::to_string(i) + "] <= sample();\n";
    text += "end endmodule";
    auto l = load(text);
    CHECK_EQ(l->m->subroutineSummaries, 1u);
    CHECK(l->m->subroutineBodyNodes < 10);
    CHECK(l->m->callUncertainties.empty());
    auto a = nodeNamed(*l->m, "top.a");
    for (int i = 0; i < 128; ++i)
        CHECK(edgeBetween(*l->m, a, nodeNamed(*l->m, "top.q[" + std::to_string(i) + "]")));
}

TEST(model_subroutine_effect_in_a_guard_keeps_its_state_target) {
    auto l = load(R"(module top(input logic clk, a, output logic q, side);
      function automatic logic predicate(); side = a; return a; endfunction
      always @(posedge clk) if (predicate()) q <= 0;
    endmodule)");
    auto side = nodeNamed(*l->m, "top.side");
    CHECK(side != kNoNode);
    CHECK(l->m->flopAt(side));
    CHECK(l->m->uncertainCallDrivers.count(side));
}

TEST(model_subroutine_uncertainty_stops_at_the_register_boundary) {
    auto l = load(R"(module top(input logic clk, a, output logic q, next_q);
      function automatic logic sample(logic v); static logic saved; saved = v; return saved; endfunction
      always_ff @(posedge clk) begin q <= sample(a); next_q <= q; end
    endmodule)");
    auto q = nodeNamed(*l->m, "top.q"), next = nodeNamed(*l->m, "top.next_q");
    CHECK(l->m->flopAt(q) && l->m->flopAt(next));
    if (!l->m->flopAt(q) || !l->m->flopAt(next)) return;
    ConeFinder cones(*l->m);
    CHECK(!cones.of(*l->m->flopAt(q))->uncertainCalls.empty());
    CHECK(cones.of(*l->m->flopAt(next))->uncertainCalls.empty());
}

namespace {
bool reaches(const Model& m, const std::string& from, const std::string& to) {
    for (const auto& e : m.edges)
        if (e.type == EdgeType::Comb && m.nameOf(e.from) == from && m.nameOf(e.to) == to) return true;
    return false;
}
} // namespace

TEST(model_bitwise_operators_keep_bits_apart) {
    auto l = load("module top(input logic [1:0] a, b, output logic [1:0] n, m, x);\n"
                  "  assign n = ~a;\n  assign m = a & b;\n  assign x = {a[0] ^ b[1], a[1]};\nendmodule\n");
    const Model& m = *l->m;
    CHECK(reaches(m, "top.a[1]", "top.n[1]"));
    CHECK(!reaches(m, "top.a[1]", "top.n[0]"));
    CHECK(reaches(m, "top.a[0]", "top.m[0]") && reaches(m, "top.b[0]", "top.m[0]"));
    CHECK(!reaches(m, "top.a[0]", "top.m[1]") && !reaches(m, "top.b[1]", "top.m[0]"));
    CHECK(reaches(m, "top.a[0]", "top.x[1]") && reaches(m, "top.b[1]", "top.x[1]"));
    CHECK(reaches(m, "top.a[1]", "top.x[0]") && !reaches(m, "top.a[0]", "top.x[0]"));
}

TEST(model_operators_that_mix_bits_stay_all_to_all) {
    auto l = load("module top(input logic [1:0] a, b, input logic s, input logic signed [1:0] sa, input logic [2:0] w,\n"
                  "           output logic [1:0] sum, mux, sh, red, trunc, output logic [3:0] ext);\n"
                  "  assign sum = a + b;\n  assign mux = s ? a : b;\n  assign sh = a << b[0];\n  assign red = {2{&a}};\n"
                  "  assign ext = sa;\n  assign trunc = 2'(w);\nendmodule\n");
    const Model& m = *l->m;
    CHECK(reaches(m, "top.a[0]", "top.sum[1]"));       // carry
    CHECK(reaches(m, "top.s", "top.mux[0]") && reaches(m, "top.s", "top.mux[1]"));   // the select reaches every bit
    CHECK(reaches(m, "top.a[1]", "top.mux[1]") && !reaches(m, "top.a[1]", "top.mux[0]"));
    CHECK(reaches(m, "top.a[0]", "top.sh[1]"));
    CHECK(reaches(m, "top.b[0]", "top.sh[0]"));
    CHECK(reaches(m, "top.a[0]", "top.red[1]"));
    CHECK(reaches(m, "top.sa[1]", "top.ext[3]") && reaches(m, "top.sa[1]", "top.ext[2]"));   // sign extension copies the top bit
    CHECK(reaches(m, "top.w[1]", "top.trunc[1]") && !reaches(m, "top.w[2]", "top.trunc[1]"));
}

TEST(model_selector_read_in_bitwise_operand_reaches_every_bit) {
    auto l = load("module top(input logic [1:0] a, input logic i, input logic [3:0] v, output logic [1:0] y);\n"
                  "  assign y = a & v[i +: 2];\nendmodule\n");
    const Model& m = *l->m;
    CHECK(reaches(m, "top.i", "top.y[0]") && reaches(m, "top.i", "top.y[1]"));
    CHECK(reaches(m, "top.v[3]", "top.y[0]"));   // a dynamic select can bring any bit
    CHECK(reaches(m, "top.a[0]", "top.y[0]") && !reaches(m, "top.a[0]", "top.y[1]"));
}

TEST(model_hubs_on_a_cycle_keep_every_source_and_finish) {
    // Two wide all-to-all assignments that feed each other, with one register source and one input on each.
    auto l = load("module top(input logic clk, input logic [47:0] a, input logic [47:0] b, output logic [47:0] q);\n"
                  "  logic [47:0] x, y, r, s;\n"
                  "  always_ff @(posedge clk) begin r <= a; s <= b; end\n"
                  "  assign x = (y + r) * a;\n"
                  "  assign y = (x + s) * b;\n"
                  "  always_ff @(posedge clk) q <= x;\nendmodule\n");
    Model& m = *l->m;
    size_t hubs = 0;
    for (const auto& node : m.nodes) hubs += node.sym == nullptr;
    CHECK(hubs >= 2);
    auto q = nodeNamed(m, "top.q[0]");
    CHECK(q != kNoNode && m.flopAt(q));
    if (q == kNoNode || !m.flopAt(q)) return;
    auto cone = sharedCones(m).of(*m.flopAt(q));
    const auto named = [&](const std::vector<ConeSrc>& sources, const std::string& name) {
        for (const auto& source : sources) if (m.nameOf(source.q).rfind(name, 0) == 0) return true;
        return false;
    };
    CHECK(named(cone->flops, "top.r[")); CHECK(named(cone->flops, "top.s["));
    CHECK(named(cone->inputs, "top.a[")); CHECK(named(cone->inputs, "top.b["));
}

TEST(repeated_edges_keep_what_is_common_whatever_their_order) {
    using namespace sverity::model;
    const auto edge = [](Branch branch, uint8_t inv, Op op, bool approximate, bool internalRead) {
        Edge e; e.from = 1; e.to = 2; e.type = EdgeType::Seq; e.ctrl = true;
        e.branch = branch; e.inv = inv; e.op = op; e.approximate = approximate; e.internalRead = internalRead;
        return e;
    };
    const Edge taken = edge(Branch::Taken, 0, Op::And, false, true), notTaken = edge(Branch::NotTaken, 1, Op::Or, true, false);
    for (const auto& pair : {std::vector<Edge>{taken, notTaken}, std::vector<Edge>{notTaken, taken}}) {
        Model m;
        m.edges = pair;
        m.compactEdges();
        CHECK_EQ(m.edges.size(), size_t{1});
        CHECK(m.edges[0].branch == Branch::None);
        CHECK_EQ(int(m.edges[0].inv), 2);
        CHECK(m.edges[0].op == Op::Other);
        CHECK(m.edges[0].approximate);
        CHECK(!m.edges[0].internalRead);
    }
}
