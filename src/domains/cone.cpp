#include "domains/cone.h"
#include <algorithm>

namespace sverity::model {

Cone ConeFinder::of(const Flop& d) {
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
    return cone;
}

Cone ConeFinder::ofNodes(const std::vector<NodeId>& nets) {
    std::vector<std::pair<NodeId, bool>> st;
    for (auto n : nets) st.push_back({n, false});
    return walk(st, kNoNode);
}

Cone ConeFinder::walk(std::vector<std::pair<NodeId, bool>> st, NodeId self) {
    Cone c;
    gen_++;
    while (!st.empty()) {
        auto [n, logic] = st.back();
        st.pop_back();
        auto& stamp = logic ? s1_ : s0_;
        if (stamp[n] == gen_) continue;
        stamp[n] = gen_;
        if (n == self) continue;
        if (m_.flopOfQ.count(n)) {
            c.flops.push_back({n, logic});
            // Only one of its clocks is known, so its launch domain is not established.
            if (m_.multiSequential.count(n)) c.unresolved.push_back(n);
            continue;
        }
        // Call uncertainty describes this node's driver, not the next-state
        // function of a register already reached at the cone boundary.
        if (auto it = m_.uncertainCallDrivers.find(n); it != m_.uncertainCallDrivers.end())
            c.uncertainCalls.insert(c.uncertainCalls.end(), it->second.begin(), it->second.end());
        if (n < m_.topInput.size() && m_.topInput[n]) { c.inputs.push_back({n, logic}); continue; }
        // A net touching a black box may be driven by it (or only read by it): record it, but keep tracing the other
        // drivers so that no crossing is lost.
        if (n < m_.blackBoxDriven.size() && m_.blackBoxDriven[n] == 1) c.bbLeaves.push_back(n);
        if (n < m_.unresolvedDriven.size() && m_.unresolvedDriven[n]) c.unresolved.push_back(n);
        if (m_.in[n].empty() && !(n < m_.blackBoxDriven.size() && m_.blackBoxDriven[n])) c.undriven.push_back(n);
        for (auto ei : m_.in[n]) {
            auto& e = m_.edges[ei];
            if (e.type == EdgeType::Comb || e.type == EdgeType::Seq) st.push_back({e.from, logic || !e.exact});
        }
    }
    return c;
}

} // namespace sverity::model
