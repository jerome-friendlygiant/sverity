// Lint rules computed on the shared dependency graph (no re-parse, no second elaboration).
#include <algorithm>
#include <set>

#include "domains/model.h"
#include "rules/rules.h"
#include "slang/ast/symbols/ValueSymbol.h"

namespace sverity {
namespace {

using namespace model;

/// LNT-012: combinational cycles, including cycles through port connections (cross-module).
class CombCycleRule final : public Rule {
public:
    std::string_view id() const override { return "LNT-012"; }
    RuleState completeness() const override { return RuleState::Partial; }
    std::string partialReason() const override {
        return "strongly-connected components of the combinational dependency graph; a node depending directly on itself "
               "is not reported, and cycles that exist only through statement ordering inside one procedure are reported "
               "as heuristic warnings (the analysis is order-insensitive there)";
    }
    void run(RuleContext& ctx) override {
        Model& m = getModel(ctx.design);
        const NodeId n = static_cast<NodeId>(m.nodes.size());
        ctx.objectsEvaluated = n;
        // iterative Tarjan over Comb edges only (Seq / AsyncCtl edges end at registers)
        std::vector<int> index(n, -1), low(n, 0);
        std::vector<char> onStack(n, 0);
        std::vector<NodeId> stack;
        int counter = 0;
        struct Frame { NodeId v; size_t next; };
        std::vector<Frame> call;
        auto combOut = [&](NodeId v) -> const std::vector<uint32_t>& { return m.out[v]; };
        for (NodeId root = 0; root < n; root++) {
            if (index[root] != -1) continue;
            call.push_back({root, 0});
            index[root] = low[root] = counter++;
            stack.push_back(root);
            onStack[root] = 1;
            while (!call.empty()) {
                Frame& f = call.back();
                const auto& oe = combOut(f.v);
                bool descended = false;
                while (f.next < oe.size()) {
                    const Edge& e = m.edges[oe[f.next++]];
                    if (e.type != EdgeType::Comb) continue;
                    NodeId w = e.to;
                    if (index[w] == -1) {
                        index[w] = low[w] = counter++;
                        stack.push_back(w);
                        onStack[w] = 1;
                        call.push_back({w, 0});
                        descended = true;
                        break;
                    } else if (onStack[w]) {
                        low[f.v] = std::min(low[f.v], index[w]);
                    }
                }
                if (descended) continue;
                NodeId v = f.v;
                if (low[v] == index[v]) {
                    std::vector<NodeId> scc;
                    NodeId w;
                    do {
                        w = stack.back();
                        stack.pop_back();
                        onStack[w] = 0;
                        scc.push_back(w);
                    } while (w != v);
                    if (scc.size() > 1) report(ctx, m, scc);
                }
                call.pop_back();
                if (!call.empty()) low[call.back().v] = std::min(low[call.back().v], low[v]);
            }
        }
    }

private:
    void report(RuleContext& ctx, Model& m, std::vector<NodeId>& scc) {
        std::set<NodeId> in(scc.begin(), scc.end());
        std::set<uint32_t> procs;
        bool collapsed = false;
        for (auto v : scc) {
            collapsed = collapsed || m.collapsed(v);
            for (auto ei : m.out[v]) {
                const Edge& e = m.edges[ei];
                if (e.type == EdgeType::Comb && in.count(e.to)) procs.insert(e.proc);
            }
        }
        bool singleProcedure = procs.size() == 1 && (m.procs[*procs.begin()].kind == ProcKind::Comb ||
                                                      m.procs[*procs.begin()].kind == ProcKind::Latch);
        std::sort(scc.begin(), scc.end());
        std::string names;
        for (size_t i = 0; i < scc.size() && i < 6; i++) names += (i ? " -> " : "") + m.nameOf(scc[i]);
        if (scc.size() > 6) names += " -> ... (" + std::to_string(scc.size()) + " signal bits)";
        Diagnostic d;
        d.ruleId = "LNT-012";
        const ProcInfo& p0 = m.procs[*procs.begin()];
        d.location = p0.loc;
        d.hierarchy = p0.instancePath;
        if (singleProcedure || collapsed) {
            d.severity = Severity::Warning;
            d.confidence = Confidence::Heuristic;
            d.message = "possible combinational cycle inside one procedure: " + names +
                        (collapsed ? " (wide signals are tracked as one node)" : " (statement order is not considered)");
            d.nextStep = "If the procedure reads a variable after assigning it, this is not a loop; otherwise break the cycle.";
        } else {
            d.severity = Severity::Error;
            d.confidence = Confidence::Established;
            d.message = "combinational cycle: " + names;
            d.nextStep = "Break the loop with a register, or restructure; combinational loops are not synthesisable in general.";
        }
        for (auto v : scc) d.signals.push_back(m.nameOf(v));
        if (d.signals.size() > 16) d.signals.resize(16);
        for (auto pi : procs) d.evidence.push_back({"data_path", "dependency through " + m.procs[pi].instancePath, m.procs[pi].loc});
        ctx.report.add(std::move(d));
    }
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeGraphLintRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<CombCycleRule>());
    return v;
}

} // namespace sverity
