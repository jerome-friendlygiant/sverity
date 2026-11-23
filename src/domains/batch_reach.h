// Graph condensation and batched reachability over the model's edges, shared by passes that need, for many registers, what
// their cone walks reach.
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <utility>
#include <vector>
#include "domains/model.h"

namespace sverity::model {

// The strongly connected components of the graph of the edges `follow` accepts (iterative Tarjan). Components are numbered in
// the order they complete, so every edge between two components runs from a higher number to a lower one.
struct Condensation {
    std::vector<uint32_t> order;          // nodes grouped by component, components in completion order
    std::vector<uint32_t> componentEnd;   // end offset in `order` of each component
    std::vector<uint32_t> componentOf;    // per node
};
template<class Follow>
Condensation condense(const Model& m, Follow follow) {
    const uint32_t count = static_cast<uint32_t>(m.nodes.size());
    constexpr uint32_t kUnseen = UINT32_MAX;
    Condensation c;
    std::vector<uint32_t> index(count, kUnseen), low(count, 0);
    std::vector<uint8_t> onStack(count, 0);
    std::vector<uint32_t> stack;
    std::vector<std::pair<uint32_t, uint32_t>> calls;   // (node, next out-edge position)
    c.order.reserve(count);
    c.componentOf.assign(count, 0);
    uint32_t next = 0;
    for (uint32_t root = 0; root < count; ++root) {
        if (index[root] != kUnseen) continue;
        calls.push_back({root, 0});
        index[root] = low[root] = next++;
        stack.push_back(root);
        onStack[root] = 1;
        while (!calls.empty()) {
            const uint32_t v = calls.back().first;
            uint32_t& pos = calls.back().second;
            const auto& out = m.out[v];
            bool descended = false;
            while (pos < out.size()) {
                const Edge& e = m.edges[out[pos++]];
                if (!follow(e)) continue;
                const uint32_t t = e.to;
                if (index[t] == kUnseen) {
                    index[t] = low[t] = next++;
                    stack.push_back(t);
                    onStack[t] = 1;
                    calls.push_back({t, 0});
                    descended = true;
                    break;
                }
                if (onStack[t]) low[v] = std::min(low[v], index[t]);
            }
            if (descended) continue;
            if (low[v] == index[v]) {
                const uint32_t component = static_cast<uint32_t>(c.componentEnd.size());
                uint32_t x;
                do {
                    x = stack.back();
                    stack.pop_back();
                    onStack[x] = 0;
                    c.componentOf[x] = component;
                    c.order.push_back(x);
                } while (x != v);
                c.componentEnd.push_back(static_cast<uint32_t>(c.order.size()));
            }
            calls.pop_back();
            if (!calls.empty()) low[calls.back().first] = std::min(low[calls.back().first], low[v]);
        }
    }
    return c;
}
// Closes two per-node bit masks under the edges `follow` accepts: afterwards every node holds the OR of its own bits and those
// of every node with a followed path to it. Components are visited sources first, so each mask is final when it is read.
template<class Follow>
void closeMasks(const Model& m, const Condensation& c, std::vector<uint64_t>& a, std::vector<uint64_t>& b, Follow follow) {
    for (size_t k = c.componentEnd.size(); k-- > 0;) {
        const uint32_t begin = k ? c.componentEnd[k - 1] : 0, end = c.componentEnd[k];
        uint64_t ma = 0, mb = 0;
        for (uint32_t i = begin; i < end; ++i) { ma |= a[c.order[i]]; mb |= b[c.order[i]]; }
        for (uint32_t i = begin; i < end; ++i) { a[c.order[i]] = ma; b[c.order[i]] = mb; }
        for (uint32_t i = begin; i < end; ++i)
            for (auto ei : m.out[c.order[i]]) {
                const Edge& e = m.edges[ei];
                if (!follow(e)) continue;
                a[e.to] |= ma;
                b[e.to] |= mb;
            }
    }
}
template<class Follow>
void closeMasks(const Model& m, std::vector<uint64_t>& a, std::vector<uint64_t>& b, Follow follow) {
    closeMasks(m, condense(m, follow), a, b, follow);
}

// Reachability of many destinations at once along the cone walk's edges (ConeFinder::walk): from a destination register's
// Seq inputs back through every node that is not a named register or top input, along Comb and Seq edges. Each of up to
// kLanes destinations is one bit lane. The graph is condensed once; a run visits the components it reaches in ascending order
// (edges between components run from higher numbers to lower ones, the walk follows them backwards), each once and only after
// every component that leads to it, so a cone shared by the batch is walked once.
class BatchWalker {
public:
    static constexpr size_t kWords = 4, kLanes = kWords * 64;
    using Lanes = std::array<uint64_t, kWords>;

    explicit BatchWalker(const Model& m) : m_(m), stops_(m.nodes.size(), 0) {
        for (NodeId n = 0; n < m.nodes.size(); ++n)
            stops_[n] = m.nodes[n].sym && (m.flopAt(n) || (n < m.topInput.size() && m.topInput[n]));
        graph_ = condense(m, [&](const Edge& e) { return follows(e); });
        acc_.assign(m.nodes.size(), Lanes{});
        pending_.assign((graph_.componentEnd.size() + 63) / 64, 0);
    }
    bool follows(const Edge& e) const { return (e.type == EdgeType::Comb || e.type == EdgeType::Seq) && !stops_[e.to]; }
    bool stops(NodeId n) const { return stops_[n]; }
    const Condensation& graph() const { return graph_; }

    // Walks lane i from the Seq inputs of destinations[i]. `pruned(n)` keeps the walk out of a node (and so out of everything
    // only reachable through it); it must give one answer for all nodes of a strongly connected component, since a component
    // is entered as a whole (a predicate of what a node reaches, such as propagated source classes or masks, does). Afterwards leaves() holds every named register or top input reached, in no particular
    // order, and lanes(n) the destinations that reached it (a register is still listed for its own lane: callers drop that).
    template<class Pruned>
    void run(const std::vector<NodeId>& destinations, Pruned pruned) {
        clear();
        const auto reach = [&](NodeId n, const Lanes& bits) {
            if (pruned(n)) return;
            Lanes& a = acc_[n];
            uint64_t fresh = 0, before = 0;
            for (size_t w = 0; w < kWords; ++w) { fresh |= bits[w] & ~a[w]; before |= a[w]; }
            if (!fresh) return;
            if (!before) { touched_.push_back(n); if (stops_[n]) leaves_.push_back(n); }
            for (size_t w = 0; w < kWords; ++w) a[w] |= bits[w];
            if (!stops_[n]) { const uint32_t c = graph_.componentOf[n]; pending_[c >> 6] |= uint64_t{1} << (c & 63); }
        };
        for (size_t lane = 0; lane < destinations.size(); ++lane) {
            Lanes bit{};
            bit[lane >> 6] = uint64_t{1} << (lane & 63);
            for (auto ei : m_.in[destinations[lane]])
                if (m_.edges[ei].type == EdgeType::Seq) reach(m_.edges[ei].from, bit);
        }
        for (size_t w = 0; w < pending_.size(); ++w)
            while (pending_[w]) {
                const uint32_t c = static_cast<uint32_t>(w * 64 + std::countr_zero(pending_[w]));
                pending_[w] &= pending_[w] - 1;
                ++components_;
                const uint32_t begin = c ? graph_.componentEnd[c - 1] : 0, end = graph_.componentEnd[c];
                Lanes all{};
                for (uint32_t i = begin; i < end; ++i)
                    for (size_t k = 0; k < kWords; ++k) all[k] |= acc_[graph_.order[i]][k];
                if (end - begin > 1)   // every node of a cycle reaches every other
                    for (uint32_t i = begin; i < end; ++i) {
                        const NodeId v = graph_.order[i];
                        bool before = false;
                        for (size_t k = 0; k < kWords; ++k) before |= acc_[v][k] != 0;
                        if (!before) touched_.push_back(v);
                        acc_[v] = all;
                    }
                for (uint32_t i = begin; i < end; ++i)
                    for (auto ei : m_.in[graph_.order[i]]) {
                        ++edgeScans_;
                        const Edge& e = m_.edges[ei];
                        if (e.type == EdgeType::Comb || e.type == EdgeType::Seq) reach(e.from, all);
                    }
            }
    }
    const std::vector<NodeId>& leaves() const { return leaves_; }
    const Lanes& lanes(NodeId n) const { return acc_[n]; }
    uint64_t components() const { return components_; }
    uint64_t edgeScans() const { return edgeScans_; }

private:
    void clear() {
        for (const NodeId n : touched_) acc_[n] = Lanes{};
        touched_.clear();
        leaves_.clear();
    }
    const Model& m_;
    std::vector<uint8_t> stops_;
    Condensation graph_;
    std::vector<Lanes> acc_;
    std::vector<uint64_t> pending_;
    std::vector<NodeId> touched_, leaves_;
    uint64_t components_ = 0, edgeScans_ = 0;
};

} // namespace sverity::model
