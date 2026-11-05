// Lint rules computed on the shared dependency graph (no re-parse, no second elaboration).
#include <algorithm>
#include <set>
#include <unordered_map>

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
    RuleState completeness() const override { return RuleState::Executed; }
    void run(RuleContext& ctx) override {
        Model& m = getModel(ctx.design);
        const NodeId n = static_cast<NodeId>(m.nodes.size());
        ctx.objectsEvaluated = n;
        // a node that depends on itself through logic is a one-node cycle
        for (auto& [node, proc] : m.selfComb) {
            const ProcInfo& pi = m.procs[proc];
            bool ordered = pi.kind == ProcKind::Comb || pi.kind == ProcKind::Latch;   // statement order decides in a procedure
            const bool dynamic = m.selfCombApproximate.count(node) != 0;
            Diagnostic d;
            d.ruleId = "LNT-012";
            d.severity = ordered || dynamic ? Severity::Warning : Severity::Error;
            d.confidence = dynamic ? Confidence::Unresolved : ordered ? Confidence::Heuristic : Confidence::Established;
            d.message = std::string(ordered ? "possible combinational self-dependency" : "combinational self-dependency") + ": " + m.nameOf(node) +
                        (ordered ? " is read and assigned in one procedure (a loop only if the read precedes the assignment)" : " drives itself through logic");
            d.location = pi.loc;
            d.hierarchy = pi.instancePath;
            d.signals = {m.nameOf(node)};
            d.nextStep = ordered ? "Assign the variable before reading it, or break the dependency with a register." : "Break the loop with a register.";
            ctx.report.add(std::move(d));
        }
        // Comb graph for cycle detection. A bit that its procedure only ever reads after writing it gets a second node for
        // that internal value: the procedure's writes feed both the bit and its internal value, and later reads in the
        // procedure come from the internal value. So `s = q; s = s ^ m;` has no cycle, while a real loop that passes
        // through the internal value still closes through the other procedures.
        struct Step { NodeId to; uint32_t proc; bool approximate; };
        std::unordered_map<uint64_t, NodeId> internal;
        std::vector<NodeId> internalOf;
        auto key = [](NodeId node, uint32_t proc) { return (static_cast<uint64_t>(node) << 32) | proc; };
        for (const Edge& e : m.edges)
            if (e.type == EdgeType::Comb && e.internalRead && internal.emplace(key(e.from, e.proc), n + internalOf.size()).second)
                internalOf.push_back(e.from);
        const NodeId total = n + static_cast<NodeId>(internalOf.size());
        std::vector<std::vector<Step>> adj(total);
        for (const Edge& e : m.edges) {
            if (e.type != EdgeType::Comb) continue;
            NodeId source = e.internalRead ? internal.at(key(e.from, e.proc)) : e.from;
            adj[source].push_back({e.to, e.proc, e.approximate});
            if (auto it = internal.find(key(e.to, e.proc)); it != internal.end()) adj[source].push_back({it->second, e.proc, e.approximate});
        }
        auto realNode = [&](NodeId v) { return v < n ? v : internalOf[v - n]; };
        std::vector<int> index(total, -1), low(total, 0);
        std::vector<char> onStack(total, 0);
        std::vector<NodeId> stack;
        int counter = 0;
        struct Frame { NodeId v; size_t next; };
        std::vector<Frame> call;
        for (NodeId root = 0; root < total; root++) {
            if (index[root] != -1) continue;
            call.push_back({root, 0});
            index[root] = low[root] = counter++;
            stack.push_back(root);
            onStack[root] = 1;
            while (!call.empty()) {
                Frame& f = call.back();
                const auto& oe = adj[f.v];
                bool descended = false;
                while (f.next < oe.size()) {
                    NodeId w = oe[f.next++].to;
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
                    // Internal values alone only describe the statement order of one activation: not a loop.
                    bool real = std::any_of(scc.begin(), scc.end(), [&](NodeId x) { return x < n && m.nodes[x].sym; });
                    if (scc.size() > 1 && real) report(ctx, m, scc, adj, n, realNode);
                }
                call.pop_back();
                if (!call.empty()) low[call.back().v] = std::min(low[call.back().v], low[v]);
            }
        }
    }

private:
    template<class Adj, class RealNode>
    void report(RuleContext& ctx, Model& m, std::vector<NodeId>& scc, const Adj& adj, NodeId n, RealNode realNode) {
        std::set<NodeId> in(scc.begin(), scc.end());
        std::set<uint32_t> procs;
        bool collapsed = false, dynamic = false;   // dynamic: the cycle may only exist because a computed index or a call reached every bit
        for (auto v : scc) {
            collapsed = collapsed || m.collapsed(realNode(v));
            for (const auto& step : adj[v])
                if (in.count(step.to)) {
                    procs.insert(step.proc);
                    dynamic = dynamic || step.approximate;
                }
        }
        // A computed index or call only lowers the finding if the loop needs it: remove those edges and look for a loop that is left.
        if (dynamic && hasPreciseCycle(m, scc, in, adj)) dynamic = false;
        // Port connections alone are one net seen from two sides, as through an inout: that is an alias, not a loop. A wire loop
        // through a child needs the child's own assignment, which is no port connection.
        if (std::all_of(procs.begin(), procs.end(), [&](uint32_t p) { return m.procs[p].kind == ProcKind::PortConnection; })) return;
        bool singleProcedure = procs.size() == 1 && (m.procs[*procs.begin()].kind == ProcKind::Comb ||
                                                      m.procs[*procs.begin()].kind == ProcKind::Latch);
        std::sort(scc.begin(), scc.end());
        std::set<NodeId> realBits;
        for (auto v : scc) if (m.nodes[realNode(v)].sym) realBits.insert(realNode(v));
        std::string names;
        size_t shown = 0;
        for (auto v : realBits) {
            if (shown == 6) break;
            names += (shown++ ? " -> " : "") + m.nameOf(v);
        }
        if (realBits.size() > 6) names += " -> ... (" + std::to_string(realBits.size()) + " signal bits)";
        Diagnostic d;
        d.ruleId = "LNT-012";
        const ProcInfo& p0 = m.procs[*procs.begin()];
        d.location = p0.loc;
        d.hierarchy = p0.instancePath;
        if (dynamic) {
            d.severity = Severity::Warning;
            d.confidence = Confidence::Unresolved;
            d.message = "possible combinational cycle through a computed index or a subroutine call: " + names + " (every bit is assumed to be reached, so whether this loop is real is not decided)";
            d.nextStep = "Check the bits the index or subroutine actually uses; if they form no loop, this is not one.";
        } else if (singleProcedure || collapsed) {
            d.severity = Severity::Warning;
            d.confidence = Confidence::Heuristic;
            d.message = "possible combinational cycle inside one procedure: " + names +
                        (collapsed ? " (wide signals are tracked as one node)" : " (a read can see the value from before the procedure)");
            d.nextStep = "If the procedure reads a variable after assigning it on every path, this is not a loop; otherwise break the cycle.";
        } else {
            d.severity = Severity::Error;
            d.confidence = Confidence::Established;
            d.message = "combinational cycle: " + names;
            d.nextStep = "Break the loop with a register, or restructure; combinational loops are not synthesisable in general.";
        }
        for (auto v : realBits) d.signals.push_back(m.nameOf(v));
        if (d.signals.size() > 16) d.signals.resize(16);
        for (auto pi : procs) d.evidence.push_back({"data_path", "dependency through " + m.procs[pi].instancePath, m.procs[pi].loc});
        addCycleWitness(d, m, scc, in, adj, n, realNode);
        ctx.report.add(std::move(d));
    }

    // Repeatedly drops nodes with no remaining precise edge in or out. A signal left over lies on, or between, precise cycles.
    template<class Adj>
    bool hasPreciseCycle(Model& m, const std::vector<NodeId>& scc, const std::set<NodeId>& in, const Adj& adj) {
        std::unordered_map<NodeId, std::pair<uint32_t, uint32_t>> degree;   // in, out
        std::unordered_map<NodeId, std::vector<NodeId>> preds;
        for (auto v : scc)
            for (const auto& step : adj[v])
                if (!step.approximate && in.count(step.to)) {
                    degree[step.to].first++;
                    degree[v].second++;
                    preds[step.to].push_back(v);
                }
        std::vector<NodeId> drop;
        std::set<NodeId> dropped;
        for (auto v : scc) if (!degree[v].first || !degree[v].second) { drop.push_back(v); dropped.insert(v); }
        while (!drop.empty()) {
            const NodeId v = drop.back();
            drop.pop_back();
            auto lose = [&](NodeId other, bool incoming) {
                auto& entry = degree[other];
                (incoming ? entry.first : entry.second)--;
                if ((!entry.first || !entry.second) && dropped.insert(other).second) drop.push_back(other);
            };
            for (const auto& step : adj[v]) if (!step.approximate && in.count(step.to)) lose(step.to, true);
            for (auto pred : preds[v]) lose(pred, false);
        }
        return std::any_of(scc.begin(), scc.end(), [&](NodeId v) { return !dropped.count(v) && v < m.nodes.size() && m.nodes[v].sym; });
    }

    // The shortest cycle through the first real bit of the component, in order: what a reviewer needs to accept or refute it.
    template<class Adj, class RealNode>
    void addCycleWitness(Diagnostic& d, Model& m, const std::vector<NodeId>& scc, const std::set<NodeId>& in, const Adj& adj, NodeId n, RealNode realNode) {
        NodeId start = kNoNode;
        for (auto v : scc) if (v < n && m.nodes[v].sym) { start = v; break; }
        if (start == kNoNode) return;
        std::unordered_map<NodeId, std::pair<NodeId, uint32_t>> cameFrom;
        std::vector<NodeId> queue{start};
        bool closed = false;
        uint32_t closingProc = 0;
        NodeId last = start;
        for (size_t i = 0; i < queue.size() && !closed; i++) {
            NodeId v = queue[i];
            for (const auto& step : adj[v]) {
                if (!in.count(step.to)) continue;
                if (step.to == start) { closed = true; closingProc = step.proc; last = v; break; }
                if (cameFrom.emplace(step.to, std::make_pair(v, step.proc)).second) queue.push_back(step.to);
            }
        }
        if (!closed) return;
        std::vector<std::pair<NodeId, uint32_t>> path{{start, closingProc}};   // (node reached, procedure of the edge into it)
        for (NodeId v = last; v != start; v = cameFrom.at(v).first) path.push_back({v, cameFrom.at(v).second});
        std::reverse(path.begin(), path.end());
        constexpr size_t kMaxWitnessSteps = 32;
        NodeId from = start;
        for (size_t i = 0; i < path.size() && i < kMaxWitnessSteps; i++) {
            auto [to, proc] = path[i];
            d.evidence.push_back({"cycle_step", m.nameOf(realNode(from)) + " -> " + m.nameOf(realNode(to)), m.procs[proc].loc});
            from = to;
        }
        if (path.size() > kMaxWitnessSteps)
            d.evidence.push_back({"cycle_step", "... and " + std::to_string(path.size() - kMaxWitnessSteps) + " more step(s) back to " + m.nameOf(start), {}});
    }
};

/// LNT-066: an X or Z constant (an exact constant driver, or a reset value) that can reach the condition of an assignment through the
/// dependency graph. Flow-insensitive: another driver or an earlier override may mask the value (Heuristic); a tri-state `z` on a net is
/// the net's normal release and is not followed.
class XReachesConditionRule final : public Rule {
public:
    std::string_view id() const override { return "LNT-066"; }
    RuleState completeness() const override { return RuleState::Executed; }
    void run(RuleContext& ctx) override {
        Model& m = getModel(ctx.design);
        ctx.objectsEvaluated = m.constDrivers.size() + m.flops.size();
        std::vector<std::tuple<NodeId, std::string, std::string>> seeds;   // node and description of the X/Z source
        std::map<NodeId, SourceLocation> sourceLocations;
        for (auto& [n, ds] : m.constDrivers)
            for (auto& d : ds) {
                if (d.value != 'x' && d.value != 'z') continue;
                bool isNet = m.nodes[n].sym && m.nodes[n].sym->kind == slang::ast::SymbolKind::Net;
                if (d.value == 'z' && isNet) continue;
                seeds.push_back({n, std::string("constant '") + d.value + "' drives " + m.nameOf(n), std::string("constant:") + d.value});
                sourceLocations[n] = m.procs[d.proc].loc;
                break;
            }
        for (auto& f : m.flops)
            for (size_t i = 0; i < f.resetValues.size(); i++)
                if (f.resetValues[i] == 'x' || f.resetValues[i] == 'z') {
                    seeds.push_back({f.q, std::string("reset value '") + f.resetValues[i] + "' of " + m.nameOf(f.q), std::string("reset:") + f.resetValues[i]});
                    break;
                }
        std::sort(seeds.begin(), seeds.end());
        for (auto& [seed, what, identity] : seeds) {
            std::set<NodeId> seen{seed};
            std::vector<NodeId> st{seed};
            const Edge* hit = nullptr;
            while (!st.empty() && !hit) {
                NodeId n = st.back();
                st.pop_back();
                for (auto ei : m.out[n]) {
                    const Edge& e = m.edges[ei];
                    if (e.type == EdgeType::AsyncCtl) continue;
                    if (e.ctrl) { hit = &e; break; }
                    if (seen.insert(e.to).second) st.push_back(e.to);
                }
            }
            if (!hit) continue;
            Diagnostic d;
            d.ruleId = "LNT-066";
            d.identityKey = identity;
            d.severity = Severity::Warning;
            d.confidence = Confidence::Heuristic;
            d.message = what + " can reach the condition of an assignment to " + m.nameOf(hit->to) + ": simulation and synthesis may disagree on the branch taken";
            d.location = m.procs[hit->proc].loc;
            if (auto it = sourceLocations.find(seed); it != sourceLocations.end())
                d.evidence.push_back({"constant_source", "X/Z source", it->second});
            d.hierarchy = m.procs[hit->proc].instancePath;
            d.signals = {m.nameOf(seed), m.nameOf(hit->to)};
            d.nextStep = "Assign a defined value (not 'x/'z) on every path that can reach a condition, or document the don't-care and guard it.";
            ctx.report.add(std::move(d));
        }
    }
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeGraphLintRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<CombCycleRule>());
    v.push_back(std::make_unique<XReachesConditionRule>());
    return v;
}

} // namespace sverity
