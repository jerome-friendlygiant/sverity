#include "domains/cone.h"
#include <algorithm>
#include <map>
#include <unordered_set>
#include <tuple>

namespace sverity::model {

// These bound retained vector payload capacity, separately for register and hub caches. Map, allocator and one uncached
// temporary cone are outside the budgets. Hubs have their own budget because recomputing one for many registers is costly.
static size_t payloadBytes(const Cone& c) {
    return c.flops.capacity() * sizeof(ConeSrc) + c.inputs.capacity() * sizeof(ConeSrc) +
           c.bbLeaves.capacity() * sizeof(NodeId) + c.unresolved.capacity() * sizeof(NodeId) +
           c.uncertainCalls.capacity() * sizeof(uint32_t) + c.undriven.capacity() * sizeof(NodeId);
}

static void normalizeUncertainty(Cone& c) {
    for (auto* nodes : {&c.bbLeaves, &c.unresolved, &c.undriven}) {
        std::sort(nodes->begin(), nodes->end());
        nodes->erase(std::unique(nodes->begin(), nodes->end()), nodes->end());
    }
    std::sort(c.uncertainCalls.begin(), c.uncertainCalls.end());
    c.uncertainCalls.erase(std::unique(c.uncertainCalls.begin(), c.uncertainCalls.end()), c.uncertainCalls.end());
}

std::shared_ptr<const Cone> ConeFinder::of(const Flop& d) {
    if (auto known = byRegister_.find(d.q); known != byRegister_.end()) return known->second;
    std::vector<std::pair<NodeId, bool>> st;
    for (auto ei : m_.in[d.q]) {
        auto& e = m_.edges[ei];
        if (e.type != EdgeType::Seq) continue;   // (a synchronous reset is an ordinary data source here)
        st.push_back({e.from, !e.exact});
    }
    auto cone = walk(st, d.q);
    if (m_.multiSequential.count(d.q)) cone.unresolved.push_back(d.q);
    if (auto it = m_.uncertainCallDrivers.find(d.q); it != m_.uncertainCallDrivers.end())
        cone.uncertainCalls.insert(cone.uncertainCalls.end(), it->second.begin(), it->second.end());
    std::sort(cone.uncertainCalls.begin(), cone.uncertainCalls.end());
    cone.uncertainCalls.erase(std::unique(cone.uncertainCalls.begin(), cone.uncertainCalls.end()), cone.uncertainCalls.end());
    // Canonical order, so bits with the same sources share one stored cone.
    const auto bySource = [](const ConeSrc& a, const ConeSrc& b) { return a.q != b.q ? a.q < b.q : a.logic < b.logic; };
    std::sort(cone.flops.begin(), cone.flops.end(), bySource);
    std::sort(cone.inputs.begin(), cone.inputs.end(), bySource);
    for (auto* nodes : {&cone.bbLeaves, &cone.unresolved, &cone.undriven}) {
        std::sort(nodes->begin(), nodes->end());
        nodes->erase(std::unique(nodes->begin(), nodes->end()), nodes->end());
    }
    size_t hash = 0;
    const auto mix = [&](size_t value) { hash = hash * 1099511628211u ^ value; };
    for (const auto& source : cone.flops) mix(source.q * 2 + source.logic);
    mix(~size_t{0});
    for (const auto& source : cone.inputs) mix(source.q * 2 + source.logic);
    for (auto* nodes : {&cone.bbLeaves, &cone.unresolved, &cone.undriven}) { mix(~size_t{0}); for (auto node : *nodes) mix(node); }
    for (auto call : cone.uncertainCalls) mix(call);
    const auto sameSources = [](const std::vector<ConeSrc>& a, const std::vector<ConeSrc>& b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const ConeSrc& x, const ConeSrc& y) { return x.q == y.q && x.logic == y.logic; });
    };
    auto found = interned_.find(hash);
    if (found != interned_.end()) for (const auto& stored : found->second)
        if (sameSources(stored->flops, cone.flops) && sameSources(stored->inputs, cone.inputs) && stored->bbLeaves == cone.bbLeaves &&
            stored->unresolved == cone.unresolved && stored->undriven == cone.undriven && stored->uncertainCalls == cone.uncertainCalls)
            return byRegister_[d.q] = stored;
    auto fresh = std::make_shared<const Cone>(std::move(cone));
    const size_t bytes = payloadBytes(*fresh);
    if (bytes > coneBudgetBytes_ - storedConeBytes_) return fresh;
    interned_[hash].push_back(fresh);
    storedConeBytes_ += bytes;
    return byRegister_[d.q] = fresh;
}

Cone ConeFinder::ofNodes(const std::vector<NodeId>& nets) {
    std::vector<std::pair<NodeId, bool>> st;
    for (auto n : nets) st.push_back({n, false});
    return walk(st, kNoNode);
}

// A hub reaches the same leaves whoever asks, and many register bits sit behind one hub: compute them once. The walk is flat, so
// a hub on a combinational cycle is just another visited node, and it stops early only at a hub whose complete result is stored.
const Cone& ConeFinder::hubCone(NodeId hub) {
    if (auto known = hubs_.find(hub); known != hubs_.end()) return known->second;
    Cone result;
    std::unordered_set<NodeId> seen{hub}, seenFlops, seenInputs;
    std::vector<NodeId> pending{hub};
    while (!pending.empty()) {
        const NodeId n = pending.back();
        pending.pop_back();
        if (m_.nodes[n].sym) {
            if (visitLeaf(n, result, seenFlops, seenInputs)) continue;
        } else if (n != hub) {
            if (auto known = hubs_.find(n); known != hubs_.end()) {
                mergeHub(known->second, kNoNode, result, seenFlops, seenInputs);
                continue;
            }
        }
        for (auto ei : m_.in[n]) {
            const auto& e = m_.edges[ei];
            if ((e.type == EdgeType::Comb || e.type == EdgeType::Seq) && seen.insert(e.from).second) pending.push_back(e.from);
        }
    }
    normalizeUncertainty(result);
    // Past the budget a hub stays uncomputed-on-demand like a register's cone: slower, same answer.
    const size_t bytes = payloadBytes(result);
    if (bytes > hubBudgetBytes_ - storedHubBytes_) { uncachedHub_ = std::move(result); return uncachedHub_; }
    storedHubBytes_ += bytes;
    return hubs_[hub] = std::move(result);
}

// Records what a node contributes to a cone. True when the walk stops here (a register or a top input).
bool ConeFinder::visitLeaf(NodeId n, Cone& c, std::unordered_set<NodeId>& flops, std::unordered_set<NodeId>& inputs) const {
    if (m_.flopOfQ.count(n)) {
        if (flops.insert(n).second) c.flops.push_back({n, true});
        // Only one of its clocks is known, so its launch domain is not established.
        if (m_.multiSequential.count(n)) c.unresolved.push_back(n);
        return true;
    }
    // Call uncertainty describes this node's driver, not the next-state
    // function of a register already reached at the cone boundary.
    if (auto it = m_.uncertainCallDrivers.find(n); it != m_.uncertainCallDrivers.end())
        c.uncertainCalls.insert(c.uncertainCalls.end(), it->second.begin(), it->second.end());
    if (n < m_.topInput.size() && m_.topInput[n]) {
        if (inputs.insert(n).second) c.inputs.push_back({n, true});
        return true;
    }
    if (n < m_.blackBoxDriven.size() && m_.blackBoxDriven[n] == 1) c.bbLeaves.push_back(n);
    if (n < m_.unresolvedDriven.size() && m_.unresolvedDriven[n]) c.unresolved.push_back(n);
    if (m_.in[n].empty() && !(n < m_.blackBoxDriven.size() && m_.blackBoxDriven[n]) && !m_.constDrivers.count(n) &&
        !m_.loopVariables.count(m_.nodes[n].sym))
        c.undriven.push_back(n);
    return false;
}

void ConeFinder::mergeHub(const Cone& hub, NodeId self, Cone& c, std::unordered_set<NodeId>& flops, std::unordered_set<NodeId>& inputs) const {
    for (const auto& source : hub.flops) if (source.q != self && flops.insert(source.q).second) c.flops.push_back({source.q, true});
    for (const auto& source : hub.inputs) if (inputs.insert(source.q).second) c.inputs.push_back({source.q, true});
    c.bbLeaves.insert(c.bbLeaves.end(), hub.bbLeaves.begin(), hub.bbLeaves.end());
    c.unresolved.insert(c.unresolved.end(), hub.unresolved.begin(), hub.unresolved.end());
    c.undriven.insert(c.undriven.end(), hub.undriven.begin(), hub.undriven.end());
    c.uncertainCalls.insert(c.uncertainCalls.end(), hub.uncertainCalls.begin(), hub.uncertainCalls.end());
}

Cone ConeFinder::walk(std::vector<std::pair<NodeId, bool>> st, NodeId self) {
    Cone c;
    gen_++;
    std::unordered_set<NodeId> viaHubFlops, viaHubInputs;   // sources already taken from a hub: all of them are `logic`
    while (!st.empty()) {
        auto [n, logic] = st.back();
        st.pop_back();
        auto& stamp = logic ? s1_ : s0_;
        if (stamp[n] == gen_) continue;
        stamp[n] = gen_;
        if (n == self) continue;
        if (!m_.nodes[n].sym) {   // a logic hub
            mergeHub(hubCone(n), self, c, viaHubFlops, viaHubInputs);
            continue;
        }
        if (m_.flopOfQ.count(n)) {
            if (!logic || viaHubFlops.insert(n).second) c.flops.push_back({n, logic});
            // Only one of its clocks is known, so its launch domain is not established.
            if (m_.multiSequential.count(n)) c.unresolved.push_back(n);
            continue;
        }
        // Call uncertainty describes this node's driver, not the next-state
        // function of a register already reached at the cone boundary.
        if (auto it = m_.uncertainCallDrivers.find(n); it != m_.uncertainCallDrivers.end())
            c.uncertainCalls.insert(c.uncertainCalls.end(), it->second.begin(), it->second.end());
        if (n < m_.topInput.size() && m_.topInput[n]) {
            if (!logic || viaHubInputs.insert(n).second) c.inputs.push_back({n, logic});
            continue;
        }
        // A net touching a black box may be driven by it (or only read by it): record it, but keep tracing the other
        // drivers so that no crossing is lost.
        if (n < m_.blackBoxDriven.size() && m_.blackBoxDriven[n] == 1) c.bbLeaves.push_back(n);
        if (n < m_.unresolvedDriven.size() && m_.unresolvedDriven[n]) c.unresolved.push_back(n);
        // A constant is a driver, and a loop index is not a signal: neither is an unmodelled source.
        if (m_.in[n].empty() && !(n < m_.blackBoxDriven.size() && m_.blackBoxDriven[n]) && !m_.constDrivers.count(n) &&
            !m_.loopVariables.count(m_.nodes[n].sym))
            c.undriven.push_back(n);
        for (auto ei : m_.in[n]) {
            auto& e = m_.edges[ei];
            if (e.type == EdgeType::Comb || e.type == EdgeType::Seq) st.push_back({e.from, logic || !e.exact});
        }
    }
    return c;
}

// The uncertainty a node adds to the cone that visits it, and whether the walk goes on past it. This mirrors `walk` and `visitLeaf`: a
// logic hub (no symbol) adds nothing and is always expanded; a register or a top input is a leaf; every other node adds what it is.
namespace {
enum : uint8_t { kBlackBox = 1, kUnresolved = 2, kCalls = 4, kUndriven = 8 };   // calls and undriven nets are reported by identity
struct Visit { uint8_t mask = 0; bool expand = true; };
}

static Visit visitOf(const Model& m, NodeId n) {
    Visit v;
    if (!m.nodes[n].sym) return v;
    if (m.flopOfQ.count(n)) {
        v.expand = false;
        if (m.multiSequential.count(n)) v.mask |= kUnresolved;
        return v;
    }
    if (m.uncertainCallDrivers.count(n)) v.mask |= kCalls;
    if (n < m.topInput.size() && m.topInput[n]) { v.expand = false; return v; }
    const bool bb = n < m.blackBoxDriven.size() && m.blackBoxDriven[n];
    if (n < m.blackBoxDriven.size() && m.blackBoxDriven[n] == 1) v.mask |= kBlackBox;
    if (n < m.unresolvedDriven.size() && m.unresolvedDriven[n]) v.mask |= kUnresolved;
    if (m.in[n].empty() && !bb && !m.constDrivers.count(n) && !m.loopVariables.count(m.nodes[n].sym)) v.mask |= kUndriven;
    return v;
}

std::vector<std::pair<uint32_t, ConeUncertainty>> ConeFinder::uncertaintyOfRegisters() {
    const NodeId count = static_cast<NodeId>(m_.nodes.size());
    // up_[n]: the uncertainty of every node a walk that reaches n would visit, n included.
    up_.assign(count, 0);
    std::vector<uint8_t> expands(count, 1);
    std::vector<NodeId> work;
    for (NodeId n = 0; n < count; ++n) {
        const Visit v = visitOf(m_, n);
        expands[n] = v.expand;
        up_[n] = v.mask;
        if (v.mask) work.push_back(n);
    }
    while (!work.empty()) {
        const NodeId n = work.back();
        work.pop_back();
        for (auto ei : m_.out[n]) {
            const auto& e = m_.edges[ei];
            if (e.type != EdgeType::Comb && e.type != EdgeType::Seq) continue;
            if (!expands[e.to] || (up_[e.to] | up_[n]) == up_[e.to]) continue;
            up_[e.to] |= up_[n];
            work.push_back(e.to);
        }
    }
    // Unresolved calls are few: each one is followed forward on its own, to the registers it reaches.
    std::map<uint32_t, std::vector<NodeId>> sourcesOf;
    for (const auto& [n, issues] : m_.uncertainCallDrivers)
        if (m_.nodes[n].sym && !m_.flopOfQ.count(n)) for (auto issue : issues) sourcesOf[issue].push_back(n);
    std::vector<std::vector<uint32_t>> callsOf(m_.flops.size());
    {
        std::vector<uint32_t> seen(count, 0);
        uint32_t round = 0;
        for (auto& [issue, nodes] : sourcesOf) {
            ++round;
            std::vector<NodeId> pending;
            for (auto n : nodes) if (seen[n] != round) { seen[n] = round; pending.push_back(n); }
            while (!pending.empty()) {
                const NodeId x = pending.back();
                pending.pop_back();
                for (auto ei : m_.out[x]) {
                    const auto& e = m_.edges[ei];
                    if (e.type != EdgeType::Comb && e.type != EdgeType::Seq) continue;
                    if (e.type == EdgeType::Seq)
                        if (auto it = m_.flopOfQ.find(e.to); it != m_.flopOfQ.end() && (callsOf[it->second].empty() || callsOf[it->second].back() != issue))
                            callsOf[it->second].push_back(issue);
                    if (expands[e.to] && seen[e.to] != round) { seen[e.to] = round; pending.push_back(e.to); }
                }
            }
        }
    }
    std::vector<std::pair<uint32_t, ConeUncertainty>> result;
    for (uint32_t di = 0; di < m_.flops.size(); ++di) {
        const NodeId q = m_.flops[di].q;
        uint8_t mask = 0;
        if (m_.multiSequential.count(q)) mask |= kUnresolved;
        for (auto ei : m_.in[q]) {
            const auto& e = m_.edges[ei];
            if (e.type == EdgeType::Seq) mask |= up_[e.from];
        }
        auto& calls = callsOf[di];
        if (auto it = m_.uncertainCallDrivers.find(q); it != m_.uncertainCallDrivers.end())
            calls.insert(calls.end(), it->second.begin(), it->second.end());
        std::sort(calls.begin(), calls.end());
        calls.erase(std::unique(calls.begin(), calls.end()), calls.end());
        if (!calls.empty()) mask |= kCalls;
        if (!mask) continue;
        ConeUncertainty u;
        u.blackBox = mask & kBlackBox;
        u.unresolved = mask & kUnresolved;
        u.undriven = mask & kUndriven;
        u.calls = std::move(calls);
        result.emplace_back(di, std::move(u));
    }
    return result;
}

std::vector<NodeId> ConeFinder::undrivenOf(const std::vector<uint32_t>& registers) const {
    std::vector<NodeId> found;
    std::unordered_set<NodeId> seen;
    std::vector<NodeId> pending;
    for (auto di : registers)
        for (auto ei : m_.in[m_.flops[di].q]) {
            const auto& e = m_.edges[ei];
            if (e.type == EdgeType::Seq && (up_[e.from] & kUndriven) && seen.insert(e.from).second) pending.push_back(e.from);
        }
    while (!pending.empty()) {
        const NodeId n = pending.back();
        pending.pop_back();
        const Visit v = visitOf(m_, n);
        if (v.mask & kUndriven) found.push_back(n);
        if (!v.expand) continue;
        for (auto ei : m_.in[n]) {
            const auto& e = m_.edges[ei];
            if ((e.type == EdgeType::Comb || e.type == EdgeType::Seq) && (up_[e.from] & kUndriven) && seen.insert(e.from).second) pending.push_back(e.from);
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

ConeFinder& sharedCones(Model& m) {
    if (!m.cones) m.cones = std::make_shared<ConeFinder>(m);
    return *m.cones;
}

} // namespace sverity::model
