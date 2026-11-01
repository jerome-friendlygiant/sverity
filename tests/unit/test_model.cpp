// Shared model: edge operators, reset values per bit, library cell kinds, black-box ports.
#include <filesystem>
#include <fstream>

#include "domains/model.h"
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
