// Lint rules computed on the shared dependency graph (no re-parse, no second elaboration).
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>

#include "domains/model.h"
#include "rules/rules.h"
#include "slang/ast/Scope.h"
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
        merged_.clear();
        // a node that depends on itself through logic is a one-node cycle; the bits of one variable with the same outcome are one finding
        struct SelfGroup { NodeId first; uint32_t proc; bool ordered, dynamic; std::vector<NodeId> bits; };
        std::vector<NodeId> selfNodes;
        for (auto& entry : m.selfComb) selfNodes.push_back(entry.first);
        std::sort(selfNodes.begin(), selfNodes.end());
        std::vector<SelfGroup> selfGroups;
        std::map<std::tuple<const void*, const void*, uint32_t, bool, bool>, size_t> selfIndex;
        for (NodeId node : selfNodes) {
            const uint32_t proc = m.selfComb.at(node);
            const ProcInfo& pi = m.procs[proc];
            // Statement order decides in a procedure, unless Flow proves the read sees the value from before the procedure, that
            // value feeds the write, and the write is the last one to reach the output: then it is a loop whatever the order.
            const bool dynamic = m.selfCombApproximate.count(node) != 0;
            const bool ordered = (pi.kind == ProcKind::Comb || pi.kind == ProcKind::Latch) && !(m.selfCombProven.count(node) && !dynamic);
            auto [it, fresh] = selfIndex.emplace(std::make_tuple(static_cast<const void*>(m.nodes[node].inst), static_cast<const void*>(m.nodes[node].sym), proc, ordered, dynamic), selfGroups.size());
            if (fresh) selfGroups.push_back({node, proc, ordered, dynamic, {}});
            selfGroups[it->second].bits.push_back(node);
        }
        for (auto& g : selfGroups) {
            const ProcInfo& pi = m.procs[g.proc];
            const bool ordered = g.ordered, dynamic = g.dynamic;
            Diagnostic d;
            d.ruleId = "LNT-012";
            d.severity = ordered || dynamic ? Severity::Warning : Severity::Error;
            d.confidence = dynamic ? Confidence::Unresolved : ordered ? Confidence::Heuristic : Confidence::Established;
            d.message = std::string(ordered ? "possible combinational self-dependency" : "combinational self-dependency") + ": " + m.nameOf(g.first) +
                        (ordered ? " is read and assigned in one procedure (a loop only if the read precedes the assignment)" : " drives itself through logic");
            if (g.bits.size() > 1)
                d.message += " (the same holds for " + std::to_string(g.bits.size()) + " bits of '" + std::string(m.nodes[g.first].sym->name) + "', from bit " +
                             std::to_string(m.nodes[g.bits.front()].bit) + " to " + std::to_string(m.nodes[g.bits.back()].bit) + "; one finding for them all)";
            d.location = pi.loc;
            d.hierarchy = pi.instancePath;
            for (NodeId bit : g.bits) { d.signals.push_back(m.nameOf(bit)); if (d.signals.size() == 16) break; }
            d.nextStep = ordered ? "Assign the variable before reading it, or break the dependency with a register." : "Break the loop with a register.";
            ctx.report.add(std::move(d));
        }
        // Comb graph for cycle detection. A bit that its procedure only ever reads after writing it gets a second node for
        // that internal value: the procedure's writes feed both the bit and its internal value, and later reads in the
        // procedure come from the internal value. So `s = q; s = s ^ m;` has no cycle, while a real loop that passes
        // through the internal value still closes through the other procedures.
        struct Step { NodeId to; uint32_t proc; bool approximate; int32_t box = -1; };
        std::unordered_map<uint64_t, NodeId> internal;
        std::vector<NodeId> internalOf;
        auto key = [](NodeId node, uint32_t proc) { return (static_cast<uint64_t>(node) << 32) | proc; };
        for (const Edge& e : m.edges)
            if (e.type == EdgeType::Comb && e.internalRead && internal.emplace(key(e.from, e.proc), n + internalOf.size()).second)
                internalOf.push_back(e.from);
        // A black box's body is unknown: each one gets a node every input feeds and every output reads, so a loop through it
        // is found. Whether the box passes an input to an output without a register is not known, so these edges are
        // approximate (Unresolved), and a component that only goes in and out of a box without any logic is not a loop. A
        // --library port declared synchronous to one of the box's clocks is registered at the boundary, and a clock port
        // carries no data, so neither takes part.
        const NodeId hubBase = n + static_cast<NodeId>(internalOf.size());
        const NodeId total = hubBase + static_cast<NodeId>(m.blackBoxes.size());
        std::vector<std::vector<Step>> adj(total);
        for (size_t b = 0; b < m.blackBoxes.size(); b++)
            for (const auto& port : m.blackBoxes[b].ports) {
                if (port.role == PortRole::Clock || !port.clock.empty()) continue;
                for (NodeId node : port.nodes) {
                    if (node >= n) continue;
                    if (port.dir != PortDir::Out) adj[node].push_back({hubBase + static_cast<NodeId>(b), kBoxProc, true, static_cast<int32_t>(b)});
                    if (port.dir != PortDir::In) adj[hubBase + b].push_back({node, kBoxProc, true, static_cast<int32_t>(b)});
                }
            }
        for (const Edge& e : m.edges) {
            if (e.type != EdgeType::Comb) continue;
            NodeId source = e.internalRead ? internal.at(key(e.from, e.proc)) : e.from;
            adj[source].push_back({e.to, e.proc, e.approximate});
            if (auto it = internal.find(key(e.to, e.proc)); it != internal.end()) adj[source].push_back({it->second, e.proc, e.approximate});
        }
        auto realNode = [&](NodeId v) { return v < n ? v : v < hubBase ? internalOf[v - n] : kNoNode; };
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
        // A loop over a vector or an array closes bit by bit, one component per bit: one finding per cycle, with the bit range.
        for (auto& group : merged_) {
            if (group.count > 1)
                group.diag.message += " (the same loop repeats at " + std::to_string(group.count) + " bit offsets of '" + group.signal + "', from bit " +
                                      std::to_string(group.lo) + " to " + std::to_string(group.hi) + "; one finding for them all)";
            ctx.report.add(std::move(group.diag));
        }
    }

private:
    struct Merged { std::string key; Diagnostic diag; size_t count = 1; uint64_t lo = 0, hi = 0; std::string signal; };
    std::vector<Merged> merged_;
    static constexpr uint32_t kBoxProc = UINT32_MAX;   // the edge goes into or out of a black box, not through a procedure

    template<class Adj, class RealNode>
    void report(RuleContext& ctx, Model& m, std::vector<NodeId>& scc, const Adj& adj, NodeId n, RealNode realNode) {
        std::set<NodeId> in(scc.begin(), scc.end());
        std::set<uint32_t> procs;
        std::set<int32_t> boxes;
        bool collapsed = false, dynamic = false;   // dynamic: the cycle may only exist because a computed index or a call reached every bit
        for (auto v : scc) {
            if (realNode(v) != kNoNode) collapsed = collapsed || m.collapsed(realNode(v));
            for (const auto& step : adj[v])
                if (in.count(step.to)) {
                    if (step.box >= 0) boxes.insert(step.box);
                    else procs.insert(step.proc);
                    dynamic = dynamic || step.approximate;
                }
        }
        // In and out of a black box with no logic between: the box may read and drive one net, which is no loop.
        if (procs.empty()) return;
        // A computed index or call only lowers the finding if the loop needs it: remove those edges and look for a loop that is left.
        if (dynamic && hasPreciseCycle(m, scc, in, adj)) { dynamic = false; boxes.clear(); }
        // Port connections alone are one net seen from two sides, as through an inout: that is an alias, not a loop. A wire loop
        // through a child needs the child's own assignment, which is no port connection.
        if (boxes.empty() && std::all_of(procs.begin(), procs.end(), [&](uint32_t p) { return m.procs[p].kind == ProcKind::PortConnection; })) return;
        bool singleProcedure = procs.size() == 1 && (m.procs[*procs.begin()].kind == ProcKind::Comb ||
                                                      m.procs[*procs.begin()].kind == ProcKind::Latch);
        std::sort(scc.begin(), scc.end());
        std::set<NodeId> realBits;
        for (auto v : scc) if (realNode(v) != kNoNode && m.nodes[realNode(v)].sym) realBits.insert(realNode(v));
        // A component whose every bit already has a certain self-dependency finding (an ERROR) says nothing more by also being a cycle
        // through approximate edges: the certain loop is the answer.
        if (dynamic && !realBits.empty() && std::all_of(realBits.begin(), realBits.end(), [&](NodeId v) {
                auto it = m.selfComb.find(v);
                if (it == m.selfComb.end() || m.selfCombApproximate.count(v)) return false;
                const auto kind = m.procs[it->second].kind;
                return !(kind == ProcKind::Comb || kind == ProcKind::Latch) || m.selfCombProven.count(v);
            }))
            return;
        // Loop counters are no hardware state: a component made of them alone is no loop.
        if (!realBits.empty() && std::all_of(realBits.begin(), realBits.end(), [&](NodeId v) { return m.nodes[v].sym && m.loopVariables.count(m.nodes[v].sym); })) return;
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
        if (!boxes.empty()) {
            const BlackBoxInst& box = m.blackBoxes[*boxes.begin()];
            d.severity = Severity::Warning;
            d.confidence = Confidence::Unresolved;
            d.message = "possible combinational cycle through black box '" + box.module + "' (" + box.hierarchy + "): " + names +
                        " (the box's body is not modelled, so whether it passes an input to an output without a register is not known)";
            d.nextStep = "Check the black box's documented input-to-output timing; if every path through it is registered, this is not a loop.";
            d.location = box.loc;
            d.hierarchy = box.hierarchy;
            for (auto b : boxes) d.evidence.push_back({"black_box", "unmodelled instance of '" + m.blackBoxes[b].module + "': " + m.blackBoxes[b].hierarchy, m.blackBoxes[b].loc});
        } else if (dynamic) {
            d.severity = Severity::Warning;
            d.confidence = Confidence::Unresolved;
            d.message = "possible combinational cycle through an approximate dependency (a computed index, a subroutine call or a primitive that is not modelled): " + names + " (every bit is assumed to be reached, so whether this loop is real is not decided)";
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
        // Components that differ only in the bit position of the same signals, procedures and outcome are one cycle.
        std::string key = std::to_string(static_cast<int>(d.severity)) + "/" + std::to_string(static_cast<int>(d.confidence));
        for (auto pi : procs) key += "|p" + std::to_string(pi);
        for (auto b : boxes) key += "|b" + std::to_string(b);
        std::set<std::pair<const void*, const void*>> signals;   // (instance, symbol): the bit position is what differs
        for (auto v : realBits) signals.insert({m.nodes[v].inst, m.nodes[v].sym});
        for (auto& [inst, sym] : signals) key += "|" + std::to_string(reinterpret_cast<uintptr_t>(inst)) + ":" + std::to_string(reinterpret_cast<uintptr_t>(sym));
        const void* mainSym = signals.begin()->second;   // the bit position is counted on this signal: its lowest bit in the component
        uint32_t bit = UINT32_MAX;
        for (auto v : realBits) if (m.nodes[v].sym == mainSym) bit = std::min(bit, m.nodes[v].bit);
        const NodeId first = *realBits.begin();
        for (auto& group : merged_) {
            if (group.key != key) continue;
            group.count++;
            group.lo = std::min<uint64_t>(group.lo, bit);
            group.hi = std::max<uint64_t>(group.hi, bit);
            return;
        }
        merged_.push_back({key, std::move(d), 1, bit, bit, std::string(static_cast<const slang::ast::ValueSymbol*>(mainSym)->name)});
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
            auto name = [&](NodeId v) { return realNode(v) != kNoNode ? m.nameOf(realNode(v)) : std::string("<black box>"); };
            d.evidence.push_back({"cycle_step", name(from) + " -> " + name(to), proc == kBoxProc ? SourceLocation{} : m.procs[proc].loc});
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
        for (auto& [n, ds] : m.constDrivers)
            for (auto& d : ds) {
                if (d.value != 'x' && d.value != 'z') continue;
                bool isNet = m.nodes[n].sym && m.nodes[n].sym->kind == slang::ast::SymbolKind::Net;
                if (d.value == 'z' && isNet) continue;
                seeds.push_back({n, std::string("constant '") + d.value + "' drives " + m.nameOf(n), std::string("constant:") + d.value});
                break;
            }
        for (auto& f : m.flops)
            for (size_t i = 0; i < f.resetValues.size(); i++)
                if (f.resetValues[i] == 'x' || f.resetValues[i] == 'z') {
                    seeds.push_back({f.q, std::string("reset value '") + f.resetValues[i] + "' of " + m.nameOf(f.q), std::string("reset:") + f.resetValues[i]});
                    break;
                }
        std::sort(seeds.begin(), seeds.end());
        seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
        // Group by source site and instance, including both branches and all assigned bits.
        using Key = std::tuple<std::string, uint32_t, uint32_t, std::string>;
        struct Hit { SourceLocation loc; std::string hierarchy; std::set<std::string> sources; std::set<std::string> calls; bool unresolved = false; };
        std::map<Key, Hit> hits;
        for (auto& [seed, what, identity] : seeds) {
            (void)identity;
            std::set<NodeId> seen{seed};
            std::vector<NodeId> stack{seed};
            while (!stack.empty()) {
                auto node = stack.back(); stack.pop_back();
                for (auto index : m.out[node]) {
                    const auto& edge = m.edges[index];
                    if (edge.type != EdgeType::AsyncCtl && seen.insert(edge.to).second) stack.push_back(edge.to);
                }
            }
            for (const auto& site : m.conditionSites) {
                if (std::none_of(site.reads.begin(), site.reads.end(), [&](NodeId n) { return seen.count(n); })) continue;
                const auto& hierarchy = m.procs[site.proc].instancePath;
                auto& hit = hits[{site.loc.file, site.loc.line, site.loc.column, hierarchy}];
                hit.loc = site.loc;
                hit.hierarchy = hierarchy;
                hit.sources.insert(what);
                if (!site.call.empty()) hit.calls.insert(site.call);
                hit.unresolved |= site.unresolved;
            }
        }
        for (const auto& [key, hit] : hits) {
            Diagnostic d;
            d.ruleId = "LNT-066";
            d.severity = Severity::Warning;
            d.confidence = hit.unresolved ? Confidence::Unresolved : Confidence::Heuristic;
            for (const auto& source : hit.sources) {
                if (!d.message.empty()) d.message += "; ";
                d.message += source;
            }
            d.message += " can reach this assignment condition: simulation and synthesis may disagree on the branch taken";
            for (const auto& call : hit.calls) d.message += "; in call to '" + call + "'";
            if (hit.unresolved) d.message += "; callee condition dependencies are unresolved";
            d.location = hit.loc;
            d.hierarchy = hit.hierarchy;
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
