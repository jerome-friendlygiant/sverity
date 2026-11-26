// Binding of the `reset_contracts` section to the elaborated design (see reset_contracts_bind.h).
#include "domains/reset_contracts_bind.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/NetType.h"

namespace sverity::model {
namespace {

constexpr int kMaxReachDepth = 16;   // recognised synchroniser hops followed when looking for a port's reset uses

const char* kCaseReason = "case analysis present; contracts not applied in 1.0";

// The reset origin of one source: exact RTL name, canonical node and the polarity of every reset use that reaches it.
struct PortAccumulator {
    std::set<bool> polarities;   // activeLow of each use
    uint32_t count = 0;
    uint32_t firstFlop = 0;
};

// Records the asynchronous reset events that reach `target`. `activeLow` is the polarity of the event whose root is `n`.
// A recognised synchroniser is followed from its last stage to its first stage, whose reset carries the assertion.
void reachPort(const Model& m, const SyncChainOf& chainOf, NodeId n, bool activeLow, NodeId target, uint32_t owner,
               PortAccumulator& acc, int depth) {
    if (n == target) {
        acc.polarities.insert(activeLow);
        if (acc.count++ == 0) acc.firstFlop = owner;
        return;
    }
    if (depth >= kMaxReachDepth) return;
    uint32_t last = 0;
    if (!m.flopBehind(n, &last)) return;
    const SyncChainFacts facts = chainOf(last);
    if (!facts.recognised || facts.first == ~0u) return;
    const uint32_t stage = facts.first;
    for (const auto& r : m.flops[stage].resets)
        reachPort(m, chainOf, r.root.node, r.activeLow, target, stage, acc, depth + 1);
}

std::string baseName(const std::string& full) {
    const auto dot = full.rfind('.');
    const size_t start = dot == std::string::npos ? 0 : dot + 1;
    const size_t end = full.find('[', start);
    return full.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string polarityName(bool activeLow) { return activeLow ? "low" : "high"; }

// SDC edge keyword to the model's edge code (1 posedge, 2 negedge); 0 when unknown.
int edgeCode(const std::string& edge) { return edge == "posedge" ? 1 : edge == "negedge" ? 2 : 0; }

using NameOf = std::function<std::string(NodeId)>;

// The structural witness of one direction: a recognised reset synchroniser whose last stage is `released`, every stage
// reset only by `driver` with the declared polarity, every stage clocked by `clockRoot` on `edge`, and no tap or hidden control.
struct Witness {
    bool matched = false;
    std::string why;            // when not matched
    SyncChainFacts chain;       // when matched
};

Witness matchWitness(const Model& m, const SyncChainOf& chainOf, NodeId driver, bool driverLow, NodeId released, NodeId clockRoot,
                     int edge, const std::string& clockName, const NameOf& nameOf) {
    Witness w;
    uint32_t last = 0;
    if (!m.flopBehind(released, &last)) {
        w.why = "'" + nameOf(released) + "' is not the output of a reset flop chain (combinational driver or bypass)";
        return w;
    }
    SyncChainFacts facts = chainOf(last);
    if (!facts.recognised) {
        w.why = "'" + nameOf(released) + "' is not a recognised reset synchroniser: " + facts.why;
        return w;
    }
    for (uint32_t s : facts.stages) {
        const Flop& f = m.flops[s];
        const std::string stage = "stage '" + nameOf(f.q) + "'";
        if (f.resets.size() != 1 || m.aliasRoot(f.resets[0].root.node) != driver) {
            w.why = stage + " is not reset only by '" + nameOf(driver) + "'";
            return w;
        }
        if (f.resets[0].activeLow != driverLow) {
            w.why = stage + " is reset with the opposite polarity to the declared source";
            return w;
        }
        if (!f.clockResolved || f.domain >= m.domains.size() || m.aliasRoot(m.domains[f.domain].root) != clockRoot || f.edge != edge) {
            w.why = stage + " is not clocked by '" + clockName + "' on the declared edge";
            return w;
        }
        if (f.hasIff) { w.why = stage + " uses an iff qualifier"; return w; }
        if (f.hasSyncResetCandidate || !f.syncResetGuards.empty() || !f.syncResetFacts.empty()) {
            w.why = stage + " has a synchronous reset or another hidden control"; return w;
        }
        if (f.extraStages != 0) { w.why = stage + " is a library synchroniser cell with hidden stages"; return w; }
        if (!f.otherClockSignals.empty()) { w.why = stage + " is also written by another sequential block"; return w; }
    }
    // Earlier stages feed only the next stage's D: no other edge or reset pin may read them.
    for (size_t j = 1; j < facts.stages.size(); j++) {
        const NodeId q = m.aliasRoot(m.flops[facts.stages[j]].q);
        const NodeId next = m.aliasRoot(m.flops[facts.stages[j - 1]].q);
        for (const Edge& e : m.edges) {
            if (e.from == kNoNode || m.aliasRoot(e.from) != q) continue;
            const NodeId to = m.aliasRoot(e.to);
            if (to == q) continue;
            if (e.type == EdgeType::Seq && !e.ctrl && to == next) continue;
            w.why = "stage '" + nameOf(m.flops[facts.stages[j]].q) + "' drives logic other than the next stage";
            return w;
        }
        for (const Flop& user : m.flops)
            for (const auto& r : user.resets)
                if (m.aliasRoot(r.root.node) == q) {
                    w.why = "stage '" + nameOf(m.flops[facts.stages[j]].q) + "' is used as an asynchronous reset";
                    return w;
                }
    }
    w.matched = true;
    w.chain = std::move(facts);
    return w;
}

// The ultimate primary input a reset root traces back to through recognised synchronisers; kNoNode with a reason otherwise.
NodeId rawInputOf(const Model& m, const SyncChainOf& chainOf, NodeId n, int depth, std::string& why) {
    if (m.topInput[n]) return n;
    if (depth >= kMaxReachDepth) { why = "the synchroniser chain is longer than the analysis limit"; return kNoNode; }
    uint32_t last = 0;
    if (!m.flopBehind(n, &last)) { why = "it is not driven by a reset synchroniser"; return kNoNode; }
    const SyncChainFacts facts = chainOf(last);
    if (!facts.recognised || facts.first == ~0u) { why = "it is not driven by a reset synchroniser: " + facts.why; return kNoNode; }
    const Flop& first = m.flops[facts.first];
    if (first.resets.size() != 1) { why = "its first stage has more than one asynchronous reset"; return kNoNode; }
    return rawInputOf(m, chainOf, m.aliasRoot(first.resets[0].root.node), depth + 1, why);
}

// Why a net symbol itself fails the exact zero-delay single-driver requirement (wor/wand/tri nets or net declaration delay, B2/B4).
std::string netSymbolGap(const Model& m, NodeId n, const NameOf& nameOf) {
    const std::string who = "'" + nameOf(n) + "'";
    if (n < m.blackBoxDriven.size() && m.blackBoxDriven[n])
        return who + " is connected to an unmodeled black box (port direction and effect are unknown)";
    if ((n < m.unresolvedDriven.size() && m.unresolvedDriven[n]) ||
        m.uncertainCallDrivers.count(n) || m.combSelfRewritten.count(n))
        return who + " is driven by an opaque, unresolved or self-updating source";
    if (const auto* sym = m.nodes[n].sym; sym && sym->kind == slang::ast::SymbolKind::Net) {
        const auto& net = sym->as<slang::ast::NetSymbol>();
        if (net.getDelay() != nullptr) return who + " has a net delay";
        const auto nk = net.netType.netKind;
        if (nk != slang::ast::NetType::Wire && nk != slang::ast::NetType::UWire)
            return who + " is a wired or tri-state net, not a single-driver wire";
    }
    return std::string();
}

// Why the zero-delay driver of node `n` is not a single exact copy (triage round 4, B2/B4): a delayed copy, an opaque or
// unresolved source, a wired/tri net, two drivers, or a copy by an always_comb or latch. Empty when `n` has one exact driver.
std::string nodeDriverGap(const Model& m, NodeId n, const NameOf& nameOf) {
    if (std::string gap = netSymbolGap(m, n, nameOf); !gap.empty()) return gap;
    const std::string who = "'" + nameOf(n) + "'";
    std::set<uint32_t> procs;
    bool delayed = false, copyKind = true;
    auto note = [&](uint32_t p) {
        procs.insert(p);
        delayed = delayed || m.procs[p].delayed;
        copyKind = copyKind && (m.procs[p].kind == ProcKind::Continuous || m.procs[p].kind == ProcKind::PortConnection);
    };
    for (uint32_t ei : m.in[n]) if (!m.isDeadEdge(m.edges[ei])) note(m.edges[ei].proc);
    if (auto it = m.constDrivers.find(n); it != m.constDrivers.end()) for (const auto& d : it->second) note(d.proc);
    if (auto it = m.foldedConstDrivers.find(n); it != m.foldedConstDrivers.end()) for (const auto& d : it->second) note(d.proc);
    if (delayed) return who + " is driven through a delayed copy";
    if (procs.size() > 1) return who + " has more than one driver";
    if (!copyKind) return who + " is copied by an always_comb or latch, not a continuous assignment";
    return std::string();
}

// The exact copy path from `n` back to its source: each node has one zero-delay driver (`nodeDriverGap`) until a register output
// or an input port. Empty when the path is a sound exact copy. Callers have already established that the path exists.
std::string copyPathGap(const Model& m, NodeId n, const NameOf& nameOf) {
    for (int guard = 0; guard < 64 && n != kNoNode && n < m.nodes.size(); guard++) {
        if (std::string gap = netSymbolGap(m, n, nameOf); !gap.empty()) return gap;
        if (auto it = m.flopOfQ.find(n); it != m.flopOfQ.end()) {
            const Flop& f = m.flops[it->second];
            if (m.procs[f.proc].delayed) return "'" + nameOf(n) + "' is a register with a delayed assignment";
            if (m.multiSequential.count(n) || !f.otherClockSignals.empty()) return "'" + nameOf(n) + "' is written by another sequential block";
            return std::string();
        }
        if (m.topInput[n]) return std::string();
        if (std::string gap = nodeDriverGap(m, n, nameOf); !gap.empty()) return gap;
        const Edge* sole = nullptr;
        int liveEdges = 0;
        for (uint32_t ei : m.in[n]) {
            if (m.isDeadEdge(m.edges[ei])) continue;
            sole = &m.edges[ei];
            liveEdges++;
        }
        if (liveEdges != 1 || !(sole->type == EdgeType::Comb && sole->exact))
            return "'" + nameOf(n) + "' is not a single exact copy";
        n = sole->from;
    }
    return "'" + nameOf(n) + "' copy path exceeds the analysis depth";
}

// A node whose value is read by an opaque instance, a library cell, a RAM, a clock gate or a primary output (triage round 4, B6).
bool fansOutOpaquely(const Model& m, NodeId q) {
    auto hits = [&](const std::vector<NodeId>& nodes) {
        for (NodeId n : nodes) if (n < m.nodes.size() && m.aliasRoot(n) == q) return true;
        return false;
    };
    for (const auto& bb : m.blackBoxes) for (const auto& p : bb.ports) if (hits(p.nodes)) return true;
    for (const auto& li : m.libInstances) for (const auto& p : li.ports) if (hits(p.nodes)) return true;
    for (const auto& r : m.rams) {
        for (const auto& p : r.ports) if (hits(p.nodes)) return true;
        if (hits(r.writeClk) || hits(r.readClk)) return true;
    }
    for (const auto& cg : m.clockGates) if (hits(cg.clk) || hits(cg.enable) || hits(cg.out)) return true;
    return q < m.topOutput.size() && m.topOutput[q];
}

// The soundness conditions of a structural release witness (CST-016, triage round 4). `driver` releases `released` through the
// chain whose stages are `chain`; `driverNamed`/`releasedNamed` are the nodes the contract names. Empty when every condition holds.
struct WitnessNodes {
    NodeId driverNamed = kNoNode, releasedNamed = kNoNode;
    NodeId driver = kNoNode, released = kNoNode;   // canonical nodes
    NodeId clockRoot = kNoNode;
    int edge = 0;
    std::string clockName;
};
std::string witnessGap(const Model& m, const SyncChainFacts& chain, const WitnessNodes& w, const std::set<NodeId>& rawInputs,
                       int64_t minStages, const NameOf& nameOf) {
    // Canonical nodes that drive a clock (flop clocks, RAM clocks, and clock-gate inputs) anywhere in the design.
    std::set<NodeId> clockNodes;
    for (const Flop& f : m.flops) {
        if (f.clockSignal != kNoNode) clockNodes.insert(m.aliasRoot(f.clockSignal));
        for (NodeId oc : f.otherClockSignals) if (oc != kNoNode) clockNodes.insert(m.aliasRoot(oc));
    }
    for (const auto& r : m.rams)
        for (const auto* list : {&r.writeClk, &r.readClk})
            for (NodeId n : *list) if (n < m.nodes.size()) clockNodes.insert(m.aliasRoot(n));
    for (const auto& cg : m.clockGates)
        for (const auto* list : {&cg.clk, &cg.enable})
            for (NodeId n : *list) if (n < m.nodes.size()) clockNodes.insert(m.aliasRoot(n));
    if (static_cast<int64_t>(chain.stages.size()) < minStages)
        return "chain shorter than the synchroniser minimum (" + std::to_string(chain.stages.size()) + " stage(s), policy requires " +
               std::to_string(minStages) + ")";
    // B2, B4: the named source and release nodes are exact single copies of the chain.
    if (auto g = copyPathGap(m, w.driverNamed, nameOf); !g.empty()) return "reset source " + g;
    if (auto g = copyPathGap(m, w.releasedNamed, nameOf); !g.empty()) return "released reset " + g;
    // Canonical nodes that drive a top-level output port (B6).
    std::set<NodeId> outputNodes;
    for (NodeId n = 0; n < m.nodes.size() && n < m.topOutput.size(); n++)
        if (m.topOutput[n]) outputNodes.insert(m.aliasRoot(n));
    for (uint32_t s : chain.stages) {
        const Flop& f = m.flops[s];
        const std::string stage = "stage '" + nameOf(f.q) + "'";
        // B2: a stage register is assigned with zero delay.
        if (m.procs[f.proc].delayed) return stage + " is assigned with a delay";
        // B3: the stage clock is the exact root, reached without a delayed or ambiguous copy.
        if (auto g = copyPathGap(m, f.clockSignal, nameOf); !g.empty()) return stage + " clock " + g;
        // B4: the stage reset input is an exact single zero-delay copy of the earlier reset.
        if (!f.resets.empty())
            if (auto g = copyPathGap(m, f.resets[0].signal, nameOf); !g.empty()) return stage + " reset " + g;
        // B6: only the next stage's D reads a stage output, except the final output.
        if (s != chain.stages.front() && (fansOutOpaquely(m, m.aliasRoot(f.q)) || clockNodes.count(m.aliasRoot(f.q)) ||
                                          outputNodes.count(m.aliasRoot(f.q))))
            return stage + " drives an opaque instance, primary output, clock or other load";
    }
    // B5: the released reset's receivers are clocked by the declared clock on the declared edge, reset only by it, with a decided force.
    for (const Flop& r : m.flops) {
        bool receives = false;
        for (const auto& x : r.resets) receives = receives || m.aliasRoot(x.root.node) == w.released;
        if (!receives) continue;
        const std::string who = "register '" + nameOf(r.q) + "'";
        if (r.resets.size() != 1 || r.resetValues.size() != 1 || r.resetConditional.size() != 1)
            return who + " reset by '" + nameOf(w.released) + "' has another asynchronous reset";
        if (auto g = copyPathGap(m, r.clockSignal, nameOf); !g.empty()) return who + " clock " + g;
        if (auto g = copyPathGap(m, r.resets[0].signal, nameOf); !g.empty()) return who + " reset " + g;
        if (r.resetConditional[0]) return who + " forces its reset value under a condition";
        if (r.resetValues[0] != '0' && r.resetValues[0] != '1') return who + " does not force a decided value on '" + nameOf(w.released) + "'";
        if (r.hasSyncResetCandidate || r.hasIff || r.blocking) return who + " has a competing or qualified reset";
        if (m.multiSequential.count(r.q) || !r.otherClockSignals.empty()) return who + " is written by another sequential block";
        if (!r.clockResolved || r.domain >= m.domains.size() || m.aliasRoot(m.domains[r.domain].root) != w.clockRoot || r.edge != w.edge)
            return who + " is not clocked by '" + w.clockName + "' on the declared edge";
    }
    // B5: the source nodes and the raw startup inputs are not clocks anywhere.
    for (NodeId n : {w.driver, w.released})
        if (clockNodes.count(n)) return "'" + nameOf(n) + "' is used as a clock";
    for (NodeId n : rawInputs)
        if (clockNodes.count(n)) return "'" + nameOf(n) + "' is the startup input and is used as a clock";
    return std::string();
}

} // namespace

const char* contractKindName(ContractKind k) {
    switch (k) {
        case ContractKind::Source: return "source";
        case ContractKind::Group: return "assert_together";
        case ContractKind::Order: return "release_order";
        case ContractKind::State: return "state";
    }
    return "";
}

const char* contractStatusName(ContractStatus s) {
    switch (s) {
        case ContractStatus::Bound: return "bound";
        case ContractStatus::Assumed: return "assumed";
        case ContractStatus::RtlRequirement: return "rtl_requirement";
        case ContractStatus::Established: return "established";
        case ContractStatus::Unapplied: return "unapplied";
        case ContractStatus::Unresolved: return "unresolved";
        case ContractStatus::Conflict: return "conflict";
    }
    return "";
}

ContractBindingResult bindResetContracts(const Model& m, const ClockRelations& rel, const ResetContracts& contracts,
                                         const sdc::Constraints& constraints, const SyncChainOf& chainOf, int64_t minStages) {
    ContractBindingResult out;
    if (!contracts.present) return out;

    // One pass over the nodes resolves every configured name exactly (scalar or simple bit, fully qualified).
    std::map<std::string, std::vector<NodeId>> byName;
    {
        std::set<std::string> wanted;
        std::set<std::string, std::less<>> bases;
        for (const auto& s : contracts.sources) {
            const std::string& name = s.port.empty() ? s.root : s.port;
            wanted.insert(name);
            bases.insert(baseName(name));
        }
        for (NodeId n = 0; n < m.nodes.size(); ++n) {
            const auto& node = m.nodes[n];
            if (!node.sym || !bases.count(node.sym->name)) continue;
            auto full = rel.rtlBitName(n);
            if (!full.empty() && wanted.count(full)) byName[full].push_back(n);
        }
    }

    // Immediate traced roots of asynchronous reset events, and the register that first uses each.
    struct RootAccumulator { std::set<bool> polarities; uint32_t count = 0; uint32_t firstFlop = 0; };
    std::unordered_map<NodeId, RootAccumulator> rootUses;
    for (uint32_t i = 0; i < m.flops.size(); i++)
        for (const auto& r : m.flops[i].resets) {
            auto& acc = rootUses[r.root.node];
            acc.polarities.insert(r.activeLow);
            if (acc.count++ == 0) acc.firstFlop = i;
        }

    auto locationOf = [&](uint32_t flop) { return m.procs[m.flops[flop].proc].loc; };
    auto bitOf = [&](NodeId n) { return contractBitName(m, rel, n); };

    // --- sources
    std::map<std::string, size_t> sourceIndex;
    std::vector<NodeId> sourceNode(contracts.sources.size(), kNoNode);   // canonical node of each source
    std::vector<NodeId> namedNode(contracts.sources.size(), kNoNode);    // the node the name denotes, before alias resolution
    for (const auto& s : contracts.sources) {
        ContractBinding b;
        b.kind = ContractKind::Source;
        b.id = s.id;
        b.polarity = s.active;
        const bool isPort = !s.port.empty();
        const std::string name = isPort ? s.port : s.root;
        b.bits = {name};
        sourceIndex[s.id] = out.entries.size();
        const bool declaredLow = s.active == "low";

        std::vector<NodeId> cands;
        if (auto it = byName.find(name); it != byName.end()) {
            namedNode[out.entries.size()] = it->second.front();
            for (NodeId n : it->second) {
                if (isPort) {
                    if (m.topInput[n]) cands.push_back(n);
                } else {
                    const NodeId canon = m.aliasRoot(n);
                    if (std::find(cands.begin(), cands.end(), canon) == cands.end()) cands.push_back(canon);
                }
            }
        }
        std::set<bool> polarities;
        NodeId node = kNoNode;
        uint32_t firstFlop = 0, uses = 0;
        if (cands.empty()) {
            b.status = ContractStatus::Unapplied;
            b.reasons.push_back(isPort ? "no exact top-level input bit named '" + name + "'" :
                                         "no exact internal node named '" + name + "' that is an asynchronous reset root");
        } else if (cands.size() > 1) {
            b.status = ContractStatus::Unapplied;
            b.reasons.push_back("name '" + name + "' resolves to more than one node");
        } else if (isPort) {
            PortAccumulator acc;
            node = cands[0];
            for (uint32_t i = 0; i < m.flops.size(); i++)
                for (const auto& r : m.flops[i].resets)
                    reachPort(m, chainOf, r.root.node, r.activeLow, node, i, acc, 0);
            polarities = acc.polarities;
            firstFlop = acc.firstFlop;
            uses = acc.count;
            if (uses == 0) {
                b.status = ContractStatus::Unapplied;
                b.reasons.push_back("not an asynchronous reset: no asynchronous reset event is driven by this input through exact copies or a recognised reset synchroniser");
            }
        } else {
            node = cands[0];
            auto it = rootUses.find(node);
            if (it == rootUses.end()) {
                b.status = ContractStatus::Unapplied;
                b.reasons.push_back("not the immediate traced root of an asynchronous reset");
            } else {
                polarities = it->second.polarities;
                firstFlop = it->second.firstFlop;
                uses = it->second.count;
            }
        }
        if (node != kNoNode && uses > 0) {
            b.bits = {bitOf(node)};
            b.evidence.push_back({std::to_string(uses) + " asynchronous reset event(s) of " + bitOf(node) + " with " +
                                  (polarities.size() == 1 ? "one polarity" : "mixed polarities"), locationOf(firstFlop)});
            if (polarities.size() > 1) {
                b.status = ContractStatus::Conflict;
                b.reasons.push_back("asynchronous reset uses of both polarities");
            } else if (*polarities.begin() != declaredLow) {
                b.status = ContractStatus::Conflict;
                b.reasons.push_back("declared active " + s.active + " but every use is active " + polarityName(*polarities.begin()));
            } else {
                b.status = ContractStatus::Bound;
            }
        }
        if (b.status != ContractStatus::Unapplied) sourceNode[out.entries.size()] = node;
        out.entries.push_back(std::move(b));
    }

    // Two source ids that name one node are a conflict for each of them.
    for (size_t i = 0; i < out.entries.size(); i++) {
        if (sourceNode[i] == kNoNode) continue;
        for (size_t j = i + 1; j < out.entries.size(); j++) {
            if (sourceNode[j] != sourceNode[i]) continue;
            for (size_t k : {i, j}) {
                auto& e = out.entries[k];
                e.status = ContractStatus::Conflict;
                const auto& other = out.entries[k == i ? j : i].id;
                e.reasons.push_back("names the same RTL node as source '" + other + "'");
            }
        }
    }

    auto sourceOf = [&](const std::string& id) -> ContractBinding& { return out.entries[sourceIndex.at(id)]; };
    auto rejected = [&](const std::string& id) {
        auto s = sourceOf(id).status;
        return s == ContractStatus::Unapplied || s == ContractStatus::Conflict;
    };
    auto isPortSource = [&](const std::string& id) { return !contracts.sources[sourceIndex.at(id)].port.empty(); };

    // Clock validity: exactly one primary SDC clock whose single source is one exact top-level input.
    auto clockProblem = [&](const std::string& clock, NodeId* rootOut = nullptr) -> std::string {
        const sdc::ClockDef* def = nullptr;
        size_t defIndex = 0;
        int defs = 0;
        for (size_t i = 0; i < constraints.clocks.size(); i++) {
            const auto& c = constraints.clocks[i];
            const std::string& name = c.name.empty() ? (c.sources.empty() ? std::string() : c.sources[0].pattern) : c.name;
            if (name == clock) { def = &c; defIndex = i; defs++; }
        }
        if (!def) return "clock '" + clock + "' is not declared by SDC";
        if (defs > 1) return "clock '" + clock + "' is declared more than once";
        if (def->generated) return "clock '" + clock + "' is generated, not a primary clock";
        if (def->sources.size() != 1) return "clock '" + clock + "' does not have exactly one source";
        int matches = 0;
        NodeId found = kNoNode;
        for (NodeId n = 0; n < m.nodes.size(); n++) {
            if (!m.topInput[n]) continue;
            if (rel.clockSourceMatches(def->sources[0], n, stripBit(m.nameOf(n)))) { matches++; found = n; }
        }
        if (matches != 1) return "clock '" + clock + "' does not name exactly one top-level input";
        // A later create_clock or create_generated_clock without -add on the same source replaces this clock there (B1).
        for (size_t j = defIndex + 1; j < constraints.clocks.size(); j++) {
            const auto& later = constraints.clocks[j];
            if (later.add) continue;
            for (const auto& src : later.sources)
                if (rel.clockSourceMatches(src, found, stripBit(m.nameOf(found))))
                    return "clock '" + clock + "' is replaced on its source by a later clock definition without -add";
        }
        if (rootOut) *rootOut = found;
        return std::string();
    };

    // --- assert_together: disjoint groups of bound primary inputs
    std::map<std::string, int> memberOf;
    for (const auto& g : contracts.assertTogether) for (const auto& id : g.sources) memberOf[id]++;
    for (const auto& g : contracts.assertTogether) {
        ContractBinding b;
        b.kind = ContractKind::Group;
        b.id = g.id;
        b.sourceIds = g.sources;
        for (const auto& id : g.sources) {
            sourceOf(id).used = true;
            b.bits.push_back(sourceOf(id).bits.front());
        }
        bool overlap = false;
        for (const auto& id : g.sources) overlap = overlap || memberOf[id] > 1;
        if (overlap) {
            b.status = ContractStatus::Conflict;
            b.reasons.push_back("overlaps another assert_together group");
        } else {
            b.status = ContractStatus::Assumed;
            for (const auto& id : g.sources) {
                if (rejected(id)) {
                    b.status = ContractStatus::Unapplied;
                    b.reasons.push_back("depends on rejected source '" + id + "'");
                    break;
                }
                if (!isPortSource(id)) {
                    b.status = ContractStatus::Unapplied;
                    b.reasons.push_back("member '" + id + "' is an internal root, not a primary input");
                    break;
                }
            }
        }
        out.entries.push_back(std::move(b));
    }

    // --- release_order
    struct OrderRef { size_t entry; std::string before, after; };
    std::vector<OrderRef> graph;
    for (const auto& o : contracts.releaseOrder) {
        ContractBinding b;
        b.kind = ContractKind::Order;
        b.id = o.id;
        b.sourceIds = {o.before, o.after};
        b.clock = o.clock;
        b.edge = o.edge;
        b.minCycles = o.minCycles;
        b.bits = {sourceOf(o.before).bits.front(), sourceOf(o.after).bits.front()};
        sourceOf(o.before).used = sourceOf(o.after).used = true;
        if (rejected(o.before) || rejected(o.after)) {
            b.status = ContractStatus::Unapplied;
            const auto& id = rejected(o.before) ? o.before : o.after;
            b.reasons.push_back("depends on rejected source '" + id + "'");
        } else if (!isPortSource(o.before) && isPortSource(o.after)) {
            b.status = ContractStatus::Unapplied;
            b.reasons.push_back("root-to-port order is outside the supported subset");
        } else {
            NodeId clockRoot = kNoNode;
            const std::string problem = clockProblem(o.clock, &clockRoot);
            if (clockRoot != kNoNode) b.clockRoot = bitOf(clockRoot);
            if (!problem.empty()) {
                b.status = ContractStatus::Unresolved;
                b.reasons.push_back(problem);
            } else if (isPortSource(o.before) && isPortSource(o.after)) {
                b.status = ContractStatus::Assumed;
            } else {
                b.status = ContractStatus::RtlRequirement;   // checked against the RTL below, after the state clauses
            }
            if (b.status != ContractStatus::Unapplied) graph.push_back({out.entries.size(), o.before, o.after});
        }
        out.entries.push_back(std::move(b));
    }
    // Duplicate (before, after) pairs and cycles among the orders that were not rejected: each participant is a conflict.
    std::map<std::pair<std::string, std::string>, int> pairCount;
    for (const auto& e : graph) pairCount[{e.before, e.after}]++;
    std::map<std::string, std::vector<std::string>> successors;
    for (const auto& e : graph) successors[e.before].push_back(e.after);
    auto reaches = [&](const std::string& from, const std::string& to) {
        std::set<std::string> seen{from};
        std::vector<std::string> work{from};
        while (!work.empty()) {
            std::string cur = work.back();
            work.pop_back();
            if (cur == to) return true;
            for (const auto& next : successors[cur]) if (seen.insert(next).second) work.push_back(next);
        }
        return false;
    };
    for (const auto& e : graph) {
        auto& b = out.entries[e.entry];
        const bool duplicate = pairCount[{e.before, e.after}] > 1;
        const bool cyclic = reaches(e.after, e.before);
        if (!duplicate && !cyclic) continue;
        b.status = ContractStatus::Conflict;
        b.reasons.push_back(duplicate ? "duplicate release relation between '" + e.before + "' and '" + e.after + "'" :
                                        "release orders form a cycle through '" + e.before + "' and '" + e.after + "'");
    }

    // --- state: initial asserted on a bound primary input
    for (const auto& st : contracts.state) {
        ContractBinding b;
        b.kind = ContractKind::State;
        b.id = st.id;
        b.sourceIds = {st.source};
        b.bits = {sourceOf(st.source).bits.front()};
        sourceOf(st.source).used = true;
        if (rejected(st.source)) {
            b.status = ContractStatus::Unapplied;
            b.reasons.push_back("depends on rejected source '" + st.source + "'");
        } else if (!isPortSource(st.source)) {
            b.status = ContractStatus::Unapplied;
            b.reasons.push_back("initial state requires a primary input port; '" + st.source + "' is an internal root");
        } else {
            b.status = ContractStatus::Assumed;
        }
        out.entries.push_back(std::move(b));
    }

    // --- release_order: the one supported RTL witness, checked against recognised reset synchroniser chains.
    for (auto& b : out.entries) {
        if (b.kind != ContractKind::Order || b.status != ContractStatus::RtlRequirement) continue;
        const size_t beforeIdx = sourceIndex.at(b.sourceIds[0]), afterIdx = sourceIndex.at(b.sourceIds[1]);
        const NodeId A = sourceNode[beforeIdx], B = sourceNode[afterIdx];
        const bool aLow = contracts.sources[beforeIdx].active == "low", bLow = contracts.sources[afterIdx].active == "low";
        NodeId X = kNoNode;
        clockProblem(b.clock, &X);
        const int edge = edgeCode(b.edge);
        const NameOf nameOf = bitOf;
        // The raw startup inputs of both sources (when traceable) must not be clocks either (CST-016 B5).
        std::set<NodeId> rawInputs;
        for (NodeId n : {A, B}) {
            std::string ignored;
            if (const NodeId raw = rawInputOf(m, chainOf, n, 0, ignored); raw != kNoNode) rawInputs.insert(raw);
        }
        // Soundness conditions of a witness whose chain releases `released` after `driver` (CST-016 triage round 4).
        auto gapOf = [&](const Witness& w, NodeId driver, NodeId driverNamed, NodeId released, NodeId releasedNamed) {
            WitnessNodes nodes{driverNamed, releasedNamed, driver, released, X, edge, b.clock};
            return witnessGap(m, w.chain, nodes, rawInputs, minStages, nameOf);
        };

        // The same pattern in reverse: the declared-earlier root is the last stage of a chain reset by the declared-later one.
        const Witness reversed = matchWitness(m, chainOf, B, bLow, A, X, edge, b.clock, nameOf);
        if (reversed.matched) {
            b.chainStages = static_cast<int64_t>(reversed.chain.stages.size());
            if (std::string gap = gapOf(reversed, B, namedNode[afterIdx], A, namedNode[beforeIdx]); !gap.empty()) {
                b.status = ContractStatus::Unresolved;
                b.reasons.push_back("no sound RTL witness for " + b.bits[0] + " released after " + b.bits[1] + ": " + gap);
                continue;
            }
            b.status = ContractStatus::Conflict;
            b.reasons.push_back("RTL releases " + b.bits[0] + " after " + b.bits[1] + ": '" + b.bits[0] + "' is the last stage of a " +
                                std::to_string(reversed.chain.stages.size()) + "-stage synchroniser reset by '" + b.bits[1] + "'");
            for (uint32_t s : reversed.chain.stages)
                b.evidence.push_back({"synchroniser stage " + nameOf(m.flops[s].q), locationOf(s)});
            continue;
        }

        const Witness forward = matchWitness(m, chainOf, A, aLow, B, X, edge, b.clock, nameOf);
        if (!forward.matched) {
            b.status = ContractStatus::Unresolved;
            b.reasons.push_back("no RTL witness for " + b.bits[0] + " released before " + b.bits[1] + ": " + forward.why);
            continue;
        }
        const int64_t L = static_cast<int64_t>(forward.chain.stages.size());
        b.chainStages = L;
        for (uint32_t s : forward.chain.stages)
            b.evidence.push_back({"synchroniser stage " + nameOf(m.flops[s].q), locationOf(s)});

        std::string why;
        const NodeId raw = rawInputOf(m, chainOf, A, 0, why);
        if (raw == kNoNode) {
            b.status = ContractStatus::Unresolved;
            b.reasons.push_back("the ultimate raw input of " + b.bits[0] + " cannot be traced: " + why);
            continue;
        }
        const std::string rawName = bitOf(raw);
        const ContractBinding* state = nullptr;
        for (const auto& e : out.entries)
            if (e.kind == ContractKind::State && e.status == ContractStatus::Assumed && !e.bits.empty() && e.bits[0] == rawName) {
                state = &e;
                break;
            }
        if (!state) {
            b.status = ContractStatus::Unresolved;
            b.reasons.push_back("no initial asserted state for " + rawName);
            continue;
        }
        if (std::string gap = gapOf(forward, A, namedNode[beforeIdx], B, namedNode[afterIdx]); !gap.empty()) {
            b.status = ContractStatus::Unresolved;
            b.reasons.push_back("no sound RTL witness for " + b.bits[0] + " released before " + b.bits[1] + ": " + gap);
            continue;
        }
        b.stateId = state->id;

        // The structural bound: A an NBA output on the same root and edge releases the chain on its release edge (bound L);
        // otherwise one coincident edge may be lost (bound L-1).
        bool sameEdgeNba = false;
        uint32_t aFlop = 0;
        if (const Flop* fa = m.flopBehind(A, &aFlop))
            sameEdgeNba = fa->clockResolved && fa->domain < m.domains.size() && m.aliasRoot(m.domains[fa->domain].root) == X &&
                          fa->edge == edge && !fa->blocking && copyPathGap(m, fa->clockSignal, nameOf).empty();
        const int64_t bound = sameEdgeNba ? L : L - 1;
        b.establishedEdges = bound;
        const std::string chainText = std::to_string(L) + "-stage synchroniser of " + b.bits[1] + " reset by " + b.bits[0];
        if (bound >= b.minCycles) {
            b.status = ContractStatus::Established;
            b.reasons.push_back("RTL releases " + b.bits[0] + " before " + b.bits[1] + ": " + chainText + ", structural bound " +
                                std::to_string(bound) + " edge(s) >= " + std::to_string(b.minCycles) + ", initial state '" + state->id + "'");
        } else if (sameEdgeNba) {
            b.status = ContractStatus::Conflict;
            b.reasons.push_back("RTL releases " + b.bits[1] + " on edge " + std::to_string(L) + " < " + std::to_string(b.minCycles) +
                                " after " + b.bits[0] + " (" + chainText + ", same clock and edge as the release)");
        } else {
            b.status = ContractStatus::Unresolved;
            b.reasons.push_back(chainText + ": structural bound " + std::to_string(bound) + " below " + std::to_string(b.minCycles) +
                                " edge(s), not decided");
        }
    }

    // Bound sources that no clause uses are not applied.
    for (size_t i = 0; i < contracts.sources.size(); i++) {
        auto& e = out.entries[i];
        if (e.status == ContractStatus::Bound && !e.used) {
            e.status = ContractStatus::Unapplied;
            e.reasons.push_back("source is not used by any clause");
        }
    }

    // A case analysis in the SDC leaves every clause unresolved, including a release-order contradiction (CST-016 B7).
    if (!constraints.caseAnalyses.empty()) {
        for (size_t i = contracts.sources.size(); i < out.entries.size(); i++) {
            auto& e = out.entries[i];
            if (e.status != ContractStatus::Conflict || e.kind == ContractKind::Order) e.status = ContractStatus::Unresolved;
            e.reasons.push_back(kCaseReason);
        }
    }
    return out;
}

std::string contractBitName(const Model& m, const ClockRelations& rel, NodeId n) {
    auto full = rel.rtlBitName(n);
    return full.empty() ? m.nameOf(n) : full;
}

std::vector<const ContractBinding*> contractsOfBit(const ContractBindingResult& result, const std::string& bit) {
    std::vector<const ContractBinding*> out;
    for (const auto& e : result.entries) {
        if (e.status == ContractStatus::Unapplied) continue;
        if (std::find(e.bits.begin(), e.bits.end(), bit) != e.bits.end()) out.push_back(&e);
    }
    return out;
}

std::vector<const ContractBinding*> ordersBetween(const ContractBindingResult& result, const std::string& a, const std::string& b) {
    std::vector<const ContractBinding*> out;
    for (const auto& e : result.entries) {
        if (e.kind != ContractKind::Order || e.status == ContractStatus::Unapplied || e.bits.size() != 2) continue;
        if ((e.bits[0] == a && e.bits[1] == b) || (e.bits[0] == b && e.bits[1] == a)) out.push_back(&e);
    }
    return out;
}

json::Value bindingsToJson(const ContractBindingResult& result) {
    auto strings = [](const std::vector<std::string>& values) {
        json::Value arr = json::Value::array();
        for (const auto& v : values) arr.push(v);
        return arr;
    };
    json::Value entries = json::Value::array();
    for (const auto& e : result.entries) {
        json::Value item = json::Value::object();
        item.set("id", e.id).set("kind", contractKindName(e.kind)).set("status", contractStatusName(e.status))
            .set("reasons", strings(e.reasons)).set("bits", strings(e.bits)).set("sources", strings(e.sourceIds));
        if (e.kind == ContractKind::Source) item.set("polarity", e.polarity);
        if (e.kind == ContractKind::Order) {
            item.set("clock", e.clock).set("edge", e.edge).set("min_cycles", e.minCycles);
            if (!e.clockRoot.empty()) item.set("clock_root", e.clockRoot);
            if (e.chainStages > 0) item.set("chain_stages", e.chainStages);
            if (e.establishedEdges >= 0) item.set("established_edges", e.establishedEdges);
            if (!e.stateId.empty()) item.set("state", e.stateId);
        }
        json::Value ev = json::Value::array();
        for (const auto& x : e.evidence) {
            json::Value line = json::Value::object();
            line.set("text", x.text);
            if (x.location.valid()) line.set("location", x.location.file + ":" + std::to_string(x.location.line));
            ev.push(std::move(line));
        }
        item.set("evidence", std::move(ev));
        entries.push(std::move(item));
    }
    return entries;
}

} // namespace sverity::model
