#include "domains/cone.h"
#include <algorithm>
#include <unordered_set>
#include <tuple>

namespace sverity::model {

// 4 bytes each. Hubs have their own budget: register cones can fill theirs on one memory-like block, and a hub recomputed for
// every register behind it costs far more than a register cone recomputed.
static constexpr size_t kMaxStoredConeEntries = size_t{1} << 29;   // about 2 GB
static constexpr size_t kMaxStoredHubEntries = size_t{1} << 27;    // about 512 MB

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
    auto& bucket = interned_[hash];
    for (const auto& stored : bucket)
        if (sameSources(stored->flops, cone.flops) && sameSources(stored->inputs, cone.inputs) && stored->bbLeaves == cone.bbLeaves &&
            stored->unresolved == cone.unresolved && stored->undriven == cone.undriven && stored->uncertainCalls == cone.uncertainCalls)
            return byRegister_[d.q] = stored;
    auto fresh = std::make_shared<const Cone>(std::move(cone));
    const size_t entries = fresh->flops.size() + fresh->inputs.size();
    if (storedEntries_ + entries > kMaxStoredConeEntries) return fresh;
    storedEntries_ += entries;
    bucket.push_back(fresh);
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
    // Past the budget a hub stays uncomputed-on-demand like a register's cone: slower, same answer.
    storedHubEntries_ += result.flops.size() + result.inputs.size();
    if (storedHubEntries_ > kMaxStoredHubEntries) { uncachedHub_ = std::move(result); return uncachedHub_; }
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

ConeFinder& sharedCones(Model& m) {
    if (!m.cones) m.cones = std::make_shared<ConeFinder>(m);
    return *m.cones;
}

} // namespace sverity::model
