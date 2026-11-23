#include "domains/cone.h"
#include <algorithm>
#include <iterator>
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

// Walks grow vectors geometrically. A retained cone can contain millions of source IDs, so keep
// only the payload that is actually used once the walk and normalization are complete.
static size_t compactPayload(Cone& c) {
    const size_t before = payloadBytes(c);
    const auto compact = [](auto& sources) {
        if (sources.capacity() - sources.size() >= 32) sources.shrink_to_fit();
    };
    compact(c.flops); compact(c.inputs); compact(c.bbLeaves);
    compact(c.unresolved); compact(c.uncertainCalls); compact(c.undriven);
    return before - payloadBytes(c);
}

static void normalizeUncertainty(Cone& c) {
    for (auto* nodes : {&c.bbLeaves, &c.unresolved, &c.undriven}) {
        std::sort(nodes->begin(), nodes->end());
        nodes->erase(std::unique(nodes->begin(), nodes->end()), nodes->end());
    }
    std::sort(c.uncertainCalls.begin(), c.uncertainCalls.end());
    c.uncertainCalls.erase(std::unique(c.uncertainCalls.begin(), c.uncertainCalls.end()), c.uncertainCalls.end());
}

// What a walk asks of every node it reaches, read from the model's hash tables once instead of at every visit.
void ConeFinder::buildKinds() {
    const NodeId count = static_cast<NodeId>(m_.nodes.size());
    kind_.assign(count, 0);
    flopIndex_.assign(count, UINT32_MAX);
    walkedThrough_.assign(count, 0);
    promoted_.assign(count, 0);
    for (const auto& [q, index] : m_.flopOfQ) { kind_[q] |= kKindFlop; flopIndex_[q] = index; }
    for (const auto q : m_.multiSequential) if (q < count) kind_[q] |= kKindMultiSequential;
    for (const auto& [n, calls] : m_.uncertainCallDrivers) if (n < count) kind_[n] |= kKindCalls;
    for (NodeId n = 0; n < count; ++n) {
        uint8_t& kind = kind_[n];
        if (n < m_.topInput.size() && m_.topInput[n]) kind |= kKindInput;
        if (n < m_.blackBoxDriven.size() && m_.blackBoxDriven[n] == 1) kind |= kKindBlackBox;
        if (n < m_.unresolvedDriven.size() && m_.unresolvedDriven[n]) kind |= kKindTruncated;
        if (m_.in[n].empty() && !(n < m_.blackBoxDriven.size() && m_.blackBoxDriven[n]) && !m_.constDrivers.count(n) &&
            !m_.loopVariables.count(m_.nodes[n].sym))
            kind |= kKindUndriven;
    }
}

std::shared_ptr<const Cone> ConeFinder::of(const Flop& d) {
    ++coneRequests_;
    if (auto known = byRegister_.find(d.q); known != byRegister_.end()) {
        ++coneRegisterCacheHits_;
        return known->second;
    }
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
    coneSourceItems_ += cone.flops.size() + cone.inputs.size() + cone.bbLeaves.size() + cone.unresolved.size() +
                        cone.uncertainCalls.size() + cone.undriven.size();
    maxConeSources_ = std::max(maxConeSources_, cone.flops.size() + cone.inputs.size());
    maxConePayloadBytes_ = std::max(maxConePayloadBytes_, payloadBytes(cone));
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
            { ++coneInternHits_; return byRegister_[d.q] = stored; }
    coneCompactedBytes_ += compactPayload(cone);
    auto fresh = std::make_shared<const Cone>(std::move(cone));
    const size_t bytes = payloadBytes(*fresh);
    if (bytes > coneBudgetBytes_ - storedConeBytes_) { ++coneBudgetMisses_; return fresh; }
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
    if (auto known = hubs_.find(hub); known != hubs_.end()) { ++hubCacheHits_; return known->second; }
    ++hubWalks_;
    Cone result;
    if (++hubGen_ == 0) {
        std::fill(hubSeenStamp_.begin(), hubSeenStamp_.end(), 0);
        std::fill(hubSourceStamp_.begin(), hubSourceStamp_.end(), 0);
        hubGen_ = 1;
    }
    const uint32_t generation = hubGen_;
    hubSeenStamp_[hub] = generation;
    std::vector<NodeId> pending{hub};
    while (!pending.empty()) {
        const NodeId n = pending.back();
        pending.pop_back();
        // A stored cone stands for everything behind its node, the node included: a logic hub, or a named node promoted to one.
        if (n != hub && (!m_.nodes[n].sym || promoted_[n] == 1))
            if (auto known = hubs_.find(n); known != hubs_.end()) {
                mergeHub(known->second, kNoNode, result, hubSourceStamp_, generation);
                continue;
            }
        if (m_.nodes[n].sym && visitLeaf(n, result, generation)) continue;
        for (auto ei : m_.in[n]) {
            const auto& e = m_.edges[ei];
            if ((e.type == EdgeType::Comb || e.type == EdgeType::Seq) && hubSeenStamp_[e.from] != generation) {
                hubSeenStamp_[e.from] = generation;
                pending.push_back(e.from);
            }
        }
    }
    normalizeUncertainty(result);
    maxHubPayloadBytes_ = std::max(maxHubPayloadBytes_, payloadBytes(result));
    hubCompactedBytes_ += compactPayload(result);
    hubSourceItems_ += result.flops.size() + result.inputs.size() + result.bbLeaves.size() + result.unresolved.size() +
                       result.uncertainCalls.size() + result.undriven.size();
    maxHubSources_ = std::max(maxHubSources_, result.flops.size() + result.inputs.size());
    // Past the budget a hub stays uncomputed-on-demand like a register's cone: slower, same answer.
    // A promoted named node has a budget of its own, so promotions never crowd out logic hubs (whose misses are the costly ones).
    const size_t bytes = payloadBytes(result);
    const bool promoted = hub < promoted_.size() && promoted_[hub] == 1;
    const size_t budget = promoted ? kPromotedBudgetBytes : hubBudgetBytes_;
    size_t& stored = promoted ? storedPromotedBytes_ : storedHubBytes_;
    if (bytes > budget - stored) {
        ++hubBudgetMisses_;
        if (!promoted) hubBudgetRefused_ = true;
        uncachedHub_ = std::move(result);
        return uncachedHub_;
    }
    stored += bytes;
    return hubs_[hub] = std::move(result);
}

// Records what a node contributes to a cone. True when the walk stops here (a register or a top input).
bool ConeFinder::visitLeaf(NodeId n, Cone& c, uint32_t generation) {
    const uint8_t kind = kind_[n];
    if (kind & kKindFlop) {
        if (hubSourceStamp_[n] != generation) {
            hubSourceStamp_[n] = generation;
            c.flops.push_back({n, true});
        }
        // Only one of its clocks is known, so its launch domain is not established.
        if (kind & kKindMultiSequential) c.unresolved.push_back(n);
        return true;
    }
    // Call uncertainty describes this node's driver, not the next-state
    // function of a register already reached at the cone boundary.
    if (kind & kKindCalls) {
        const auto& calls = m_.uncertainCallDrivers.at(n);
        c.uncertainCalls.insert(c.uncertainCalls.end(), calls.begin(), calls.end());
    }
    if (kind & kKindInput) {
        if (hubSourceStamp_[n] != generation) {
            hubSourceStamp_[n] = generation;
            c.inputs.push_back({n, true});
        }
        return true;
    }
    if (kind & kKindBlackBox) c.bbLeaves.push_back(n);
    if (kind & kKindTruncated) c.unresolved.push_back(n);
    if (kind & kKindUndriven) c.undriven.push_back(n);
    return false;
}

void ConeFinder::mergeHub(const Cone& hub, NodeId self, Cone& c, std::vector<uint32_t>& stamps, uint32_t generation) {
    for (const auto& source : hub.flops) if (source.q != self && stamps[source.q] != generation) {
        stamps[source.q] = generation;
        c.flops.push_back({source.q, true});
    }
    for (const auto& source : hub.inputs) if (stamps[source.q] != generation) {
        stamps[source.q] = generation;
        c.inputs.push_back({source.q, true});
    }
    c.bbLeaves.insert(c.bbLeaves.end(), hub.bbLeaves.begin(), hub.bbLeaves.end());
    c.unresolved.insert(c.unresolved.end(), hub.unresolved.begin(), hub.unresolved.end());
    c.undriven.insert(c.undriven.end(), hub.undriven.begin(), hub.undriven.end());
    c.uncertainCalls.insert(c.uncertainCalls.end(), hub.uncertainCalls.begin(), hub.uncertainCalls.end());
}

Cone ConeFinder::walk(std::vector<std::pair<NodeId, bool>> st, NodeId self) {
    ++coneWalks_;
    Cone c;
    if (++gen_ == 0) {
        std::fill(s0_.begin(), s0_.end(), 0);
        std::fill(s1_.begin(), s1_.end(), 0);
        std::fill(sourceStamp_.begin(), sourceStamp_.end(), 0);
        gen_ = 1;
    }
    const uint32_t generation = gen_;
    while (!st.empty()) {
        auto [n, logic] = st.back();
        st.pop_back();
        auto& stamp = logic ? s1_ : s0_;
        if (stamp[n] == generation) continue;
        stamp[n] = generation;
        if (n == self) continue;
        if (!m_.nodes[n].sym) {   // a logic hub
            mergeHub(hubCone(n), self, c, sourceStamp_, generation);
            continue;
        }
        const uint8_t kind = kind_[n];
        if (kind & kKindFlop) {
            if (!logic || sourceStamp_[n] != generation) {
                if (logic) sourceStamp_[n] = generation;
                c.flops.push_back({n, logic});
            }
            // Only one of its clocks is known, so its launch domain is not established.
            if (kind & kKindMultiSequential) c.unresolved.push_back(n);
            continue;
        }
        // Call uncertainty describes this node's driver, not the next-state
        // function of a register already reached at the cone boundary.
        if (kind & kKindCalls) {
            const auto& calls = m_.uncertainCallDrivers.at(n);
            c.uncertainCalls.insert(c.uncertainCalls.end(), calls.begin(), calls.end());
        }
        if (kind & kKindInput) {
            if (!logic || sourceStamp_[n] != generation) {
                if (logic) sourceStamp_[n] = generation;
                c.inputs.push_back({n, logic});
            }
            continue;
        }
        // A net touching a black box may be driven by it (or only read by it): record it, but keep tracing the other
        // drivers so that no crossing is lost.
        if (kind & kKindBlackBox) c.bbLeaves.push_back(n);
        if (kind & kKindTruncated) c.unresolved.push_back(n);
        // A constant is a driver, and a loop index is not a signal: neither is an unmodelled source.
        if (kind & kKindUndriven) c.undriven.push_back(n);
        for (auto ei : m_.in[n]) {
            auto& e = m_.edges[ei];
            if (e.type == EdgeType::Comb || e.type == EdgeType::Seq) st.push_back({e.from, logic || !e.exact});
        }
    }
    return c;
}

std::vector<std::pair<std::string, uint64_t>> ConeFinder::telemetry() const {
    return {
        {"cdc.cones.register_requests", coneRequests_},
        {"cdc.cones.register_cache_hits", coneRegisterCacheHits_},
        {"cdc.cones.walks", coneWalks_},
        {"cdc.cones.intern_hits", coneInternHits_},
        {"cdc.cones.budget_misses", coneBudgetMisses_},
        {"cdc.cones.retained_payload_bytes", storedConeBytes_},
        {"cdc.cones.compacted_payload_bytes", coneCompactedBytes_},
        {"cdc.cones.source_scratch_bytes", 3 * m_.nodes.size() * sizeof(uint32_t)},
        {"cdc.cones.max_working_payload_bytes", maxConePayloadBytes_},
        {"cdc.cones.max_source_items", maxConeSources_},
        {"cdc.cones.walked_source_items", coneSourceItems_},
        {"cdc.hubs.cache_hits", hubCacheHits_},
        {"cdc.hubs.walks", hubWalks_},
        {"cdc.hubs.budget_misses", hubBudgetMisses_},
        {"cdc.hubs.retained_payload_bytes", storedHubBytes_},
        {"cdc.hubs.compacted_payload_bytes", hubCompactedBytes_},
        {"cdc.hubs.max_working_payload_bytes", maxHubPayloadBytes_},
        {"cdc.hubs.max_source_items", maxHubSources_},
        {"cdc.hubs.walked_source_items", hubSourceItems_},
        {"cdc.views.walks", coneViews_},
        {"cdc.views.skipped_subgraphs", viewSkippedSubgraphs_},
        {"cdc.views.listed_source_items", viewListedSources_},
        {"cdc.views.visited_nodes", viewVisitedNodes_},
        {"cdc.views.promoted_nodes", viewPromotedNodes_},
        {"cdc.views.hubs_walked_through", viewHubsWalkedThrough_},
        {"cdc.views.promoted_retained_bytes", storedPromotedBytes_},
        {"cdc.views.open_cone_walks", openConeWalks_},
        {"cdc.views.open_cone_hits", openConeHits_},
        {"cdc.views.open_cone_budget_misses", openConeMisses_},
        {"cdc.views.open_cone_retained_bytes", openConeBytes_},
        {"cdc.views.visited_open_nodes", viewOpenNodes_},
        {"cdc.views.hub_merges", viewHubMerges_},
        {"cdc.views.hub_sources_filtered", viewHubSources_},
    };
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

bool ConeFinder::hasUncertainCalls(uint32_t flop) {
    if (callsOfRegister_.empty()) {
        callsOfRegister_.assign(m_.flops.size(), 0);
        for (const auto& [di, u] : uncertaintyOfRegisters()) if (!u.calls.empty()) callsOfRegister_[di] = 1;
    }
    return callsOfRegister_[flop] != 0;
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

size_t SourceClasses::bytes() const {
    size_t total = (flopClass_.capacity() + flopSet_.capacity() + setOf_.capacity()) * sizeof(uint32_t) + open_.capacity() +
                   (fewA_.capacity() + fewB_.capacity()) * sizeof(NodeId) + sets_.capacity() * sizeof(std::vector<uint32_t>);
    for (const auto& set : sets_) total += set.capacity() * sizeof(uint32_t);
    return total;
}

// Forward over the edges a walk follows backwards, so a node's set is the union over everything a walk from it would reach: a register
// or a top input ends the walk (it is a leaf), any other node is expanded. The lattice is finite (a set grows to at most 64 classes,
// then kWide; at most two registers, then many), so the work list ends.
SourceClasses ConeFinder::classify(std::vector<uint32_t> flopClass, bool complete) {
    constexpr uint32_t kWide = SourceClasses::kWide;
    constexpr size_t kMaxClasses = 64;
    SourceClasses r;
    r.complete_ = complete;
    r.serial_ = ++classifications_;
    r.flopClass_ = std::move(flopClass);
    r.sets_.push_back({});
    std::map<std::vector<uint32_t>, uint32_t> ids{{{}, 0}};
    const auto intern = [&](std::vector<uint32_t> set) -> uint32_t {
        if (set.size() > kMaxClasses) return kWide;
        auto [it, fresh] = ids.try_emplace(std::move(set), static_cast<uint32_t>(r.sets_.size()));
        if (fresh) r.sets_.push_back(it->first);
        return it->second;
    };
    std::unordered_map<uint64_t, uint32_t> joined;
    const auto join = [&](uint32_t a, uint32_t b) -> uint32_t {
        if (a == b || b == 0) return a;
        if (a == 0) return b;
        if (a == kWide || b == kWide) return kWide;
        const uint64_t key = a < b ? (uint64_t{a} << 32 | b) : (uint64_t{b} << 32 | a);
        if (auto it = joined.find(key); it != joined.end()) return it->second;
        std::vector<uint32_t> both;
        const auto& x = r.sets_[a];
        const auto& y = r.sets_[b];
        std::set_union(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(both));
        const uint32_t id = intern(std::move(both));
        joined.emplace(key, id);
        return id;
    };
    r.flopSet_.resize(m_.flops.size());
    for (uint32_t fi = 0; fi < m_.flops.size(); ++fi) {
        const uint32_t cls = r.flopClass_[fi];
        r.flopSet_[fi] = cls == SourceClasses::kIgnore ? 0 : cls == SourceClasses::kListed ? kWide : intern({cls});
    }
    const NodeId count = static_cast<NodeId>(m_.nodes.size());
    r.setOf_.assign(count, 0);
    if (complete) { r.fewA_.assign(count, kNoNode); r.fewB_.assign(count, kNoNode); r.open_.assign(count, 0); }
    std::vector<uint8_t> expands(count, 1);
    std::vector<NodeId> work;
    for (NodeId n = 0; n < count; ++n) {
        const Visit v = visitOf(m_, n);
        expands[n] = v.expand;
        uint32_t& set = r.setOf_[n];
        if (auto it = m_.flopOfQ.find(n); it != m_.flopOfQ.end()) {
            set = r.flopSet_[it->second];
            if (complete) r.fewA_[n] = n;
        } else if (!v.expand && complete) {
            r.open_[n] = 1;   // a top input: always listed
        }
        if (complete && v.mask) r.open_[n] = 1;
        if (set != 0 || (complete && (r.fewA_[n] != kNoNode || r.open_[n]))) work.push_back(n);
    }
    const auto addFew = [&](NodeId& a, NodeId& b, NodeId x) {
        if (x == kNoNode || b == ConeView::kMany || a == x || b == x) return false;
        if (a == kNoNode) a = x;
        else if (b == kNoNode) b = x;
        else b = ConeView::kMany;
        return true;
    };
    while (!work.empty()) {
        const NodeId n = work.back();
        work.pop_back();
        for (auto ei : m_.out[n]) {
            const auto& e = m_.edges[ei];
            if (e.type != EdgeType::Comb && e.type != EdgeType::Seq) continue;
            const NodeId t = e.to;
            if (!expands[t]) continue;
            bool changed = false;
            const uint32_t set = join(r.setOf_[t], r.setOf_[n]);
            if (set != r.setOf_[t]) { r.setOf_[t] = set; changed = true; }
            if (complete) {
                if (r.open_[n] && !r.open_[t]) { r.open_[t] = 1; changed = true; }
                NodeId &a = r.fewA_[t], &b = r.fewB_[t];
                if (r.fewB_[n] == ConeView::kMany) {
                    if (b != ConeView::kMany) { b = ConeView::kMany; changed = true; }
                } else {
                    changed |= addFew(a, b, r.fewA_[n]);
                    changed |= addFew(a, b, r.fewB_[n]);
                }
            }
            if (changed) work.push_back(t);
        }
    }
    return r;
}

// `walk` with two changes: a node whose set `skip` accepts is summarised instead of walked, and a register source whose own class it
// accepts is summarised instead of listed. Everything else is reached on the same paths with the same `logic` flags, so it is listed
// exactly as `of` lists it.
ConeView ConeFinder::viewImpl(const Flop& d, const SourceClasses& cl, uint64_t key, const std::function<bool(uint32_t)>& askSkip) {
    constexpr uint32_t kWide = SourceClasses::kWide;
    ConeView v;
    if (memoClasses_ != cl.serial_ || memoKey_ != key || memoStamp_.size() != cl.sets_.size()) {
        memoClasses_ = cl.serial_;
        memoKey_ = key;
        if (memoStamp_.size() != cl.sets_.size()) {
            memoStamp_.assign(cl.sets_.size(), 0);
            memoAnswer_.assign(cl.sets_.size(), 0);
            memoRound_ = 0;
        }
        if (++memoRound_ == 0) { std::fill(memoStamp_.begin(), memoStamp_.end(), 0); memoRound_ = 1; }
        filteredHubs_.clear();
    }
    const auto skip = [&](uint32_t id) {
        if (memoStamp_[id] != memoRound_) { memoStamp_[id] = memoRound_; memoAnswer_[id] = askSkip(id); }
        return memoAnswer_[id] != 0;
    };
    const NodeId self = d.q;
    std::vector<std::pair<NodeId, bool>> st;
    for (auto ei : m_.in[d.q]) {
        auto& e = m_.edges[ei];
        if (e.type != EdgeType::Seq) continue;
        st.push_back({e.from, !e.exact});
    }
    ++coneViews_;
    if (++gen_ == 0) {
        std::fill(s0_.begin(), s0_.end(), 0);
        std::fill(s1_.begin(), s1_.end(), 0);
        std::fill(sourceStamp_.begin(), sourceStamp_.end(), 0);
        gen_ = 1;
    }
    const uint32_t generation = gen_;
    const auto note = [&](NodeId x) {
        if (x == kNoNode || v.skippedMany || v.skippedA == x || v.skippedB == x) return;
        if (v.skippedA == kNoNode) v.skippedA = x;
        else if (v.skippedB == kNoNode) v.skippedB = x;
        else v.skippedMany = true;
    };
    const auto skipSet = [&](uint32_t id, NodeId a, NodeId b) {
        if (id == kWide || !skip(id)) return false;
        const auto& classes = cl.sets_[id];
        v.skippedClasses.insert(v.skippedClasses.end(), classes.begin(), classes.end());
        if (b == ConeView::kMany) v.skippedMany = true;
        else { note(a); note(b); }
        ++viewSkippedSubgraphs_;
        return true;
    };
    const auto skipNode = [&](NodeId n) {
        return !cl.open(n) && skipSet(cl.setOf_[n], cl.complete_ ? cl.fewA_[n] : kNoNode, cl.complete_ ? cl.fewB_[n] : kNoNode);
    };
    const auto skipFlop = [&](NodeId q) { return skipSet(cl.flopSet_[flopIndex_[q]], q, kNoNode); };
    // The stored flat cone of `n` (a logic hub, or a node promoted to one), filtered like single registers: everything behind
    // it is reached through logic.
    const auto mergeFlat = [&](NodeId n) {
        // Registers all accepted, but an input or uncertainty to list: those come from the stored cone, the registers do not.
        const bool registersSkipped = cl.open(n) && skipSet(cl.setOf_[n], cl.fewA_[n], cl.fewB_[n]);
        const Cone& hub = hubCone(n);
        ++viewHubMerges_;
        if (!registersSkipped) viewHubSources_ += hub.flops.size();
        if (!registersSkipped) {
            auto [cached, fresh] = filteredHubs_.try_emplace(n);
            FilteredHub& filtered = cached->second;
            if (fresh) {
                std::vector<uint32_t> skippedSets;
                for (const auto& source : hub.flops) {
                    const uint32_t set = cl.flopSet_[flopIndex_[source.q]];
                    if (set != kWide && skip(set)) {
                        if (skippedSets.empty() || skippedSets.back() != set) skippedSets.push_back(set);
                        const NodeId q = source.q;
                        if (filtered.skippedMany || filtered.skippedA == q || filtered.skippedB == q) continue;
                        if (filtered.skippedA == kNoNode) filtered.skippedA = q;
                        else if (filtered.skippedB == kNoNode) filtered.skippedB = q;
                        else filtered.skippedMany = true;
                    } else {
                        filtered.listed.push_back(source.q);
                    }
                }
                std::sort(skippedSets.begin(), skippedSets.end());
                skippedSets.erase(std::unique(skippedSets.begin(), skippedSets.end()), skippedSets.end());
                filtered.skippedSets = std::move(skippedSets);
            }
            // The register itself is never a source of its own cone: it is left out here as `walk` leaves it out.
            for (const NodeId q : filtered.listed) if (q != self && sourceStamp_[q] != generation) {
                sourceStamp_[q] = generation;
                v.flops.push_back({q, true});
            }
            for (const uint32_t set : filtered.skippedSets) {
                const auto& classes = cl.sets_[set];
                v.skippedClasses.insert(v.skippedClasses.end(), classes.begin(), classes.end());
            }
            if (filtered.skippedMany) v.skippedMany = true;
            else { note(filtered.skippedA); note(filtered.skippedB); }
        }
        for (const auto& source : hub.inputs) if (sourceStamp_[source.q] != generation) {
            sourceStamp_[source.q] = generation;
            v.inputs.push_back({source.q, true});
        }
        v.bbLeaves.insert(v.bbLeaves.end(), hub.bbLeaves.begin(), hub.bbLeaves.end());
        v.unresolved.insert(v.unresolved.end(), hub.unresolved.begin(), hub.unresolved.end());
        v.undriven.insert(v.undriven.end(), hub.undriven.begin(), hub.undriven.end());
        v.uncertainCalls.insert(v.uncertainCalls.end(), hub.uncertainCalls.begin(), hub.uncertainCalls.end());
    };
    while (!st.empty()) {
        auto [n, logic] = st.back();
        st.pop_back();
        auto& stamp = logic ? s1_ : s0_;
        if (stamp[n] == generation) continue;
        stamp[n] = generation;
        if (n == self) continue;
        ++viewVisitedNodes_;
        if (cl.open(n)) ++viewOpenNodes_;
        if (!m_.nodes[n].sym) {   // a logic hub: its stored flat cone, filtered like single registers
            if (skipNode(n)) continue;
            // Once the hub budget has refused a cone, a hub not already stored would be flattened again at every visit: walk through
            // it instead. A hub holds no uncertainty itself and everything behind it is reached through logic, so the view is the same.
            if (!hubBudgetRefused_ || hubs_.count(n)) { mergeFlat(n); continue; }
            ++viewHubsWalkedThrough_;
            for (auto ei : m_.in[n]) {
                const auto& e = m_.edges[ei];
                if (e.type == EdgeType::Comb || e.type == EdgeType::Seq) st.push_back({e.from, true});
            }
            continue;
        }
        const uint8_t kind = kind_[n];
        if (kind & kKindFlop) {
            if (!logic || sourceStamp_[n] != generation) {
                if (logic) sourceStamp_[n] = generation;
                if (!skipFlop(n)) v.flops.push_back({n, logic});
            }
            if (kind & kKindMultiSequential) v.unresolved.push_back(n);
            continue;
        }
        const bool input = kind & kKindInput;
        if (!input && skipNode(n)) continue;
        // Reached through logic, registers all accepted, an input or uncertainty to list: those come from the node's stored lists.
        if (!input && logic && cl.open(n) && skipSet(cl.setOf_[n], cl.fewA_[n], cl.fewB_[n])) {
            const OpenCone& open = openConeOf(n, cl);
            for (const NodeId x : open.inputs) if (sourceStamp_[x] != generation) {
                sourceStamp_[x] = generation;
                v.inputs.push_back({x, true});
            }
            v.bbLeaves.insert(v.bbLeaves.end(), open.bbLeaves.begin(), open.bbLeaves.end());
            v.unresolved.insert(v.unresolved.end(), open.unresolved.begin(), open.unresolved.end());
            v.undriven.insert(v.undriven.end(), open.undriven.begin(), open.undriven.end());
            v.uncertainCalls.insert(v.uncertainCalls.end(), open.uncertainCalls.begin(), open.uncertainCalls.end());
            continue;
        }
        // A named node that views keep walking through logic is treated as a hub from then on: its flat cone is stored once and
        // filtered once per skip key, instead of walked again for every register behind it. The walk from it lists the same.
        if (!input && logic && m_.nodes[n].sym) {
            if (promoted_[n] == 1) { mergeFlat(n); continue; }
            if (promoted_[n] == 0 && walkedThrough_[n] < UINT8_MAX && ++walkedThrough_[n] >= kPromoteAfter &&
                storedPromotedBytes_ < kPromotedBudgetBytes / 2) {
                promoted_[n] = 1;
                ++viewPromotedNodes_;
                mergeFlat(n);
                // A cone the hub budget cannot hold would be recomputed in full at every visit: walk this node again instead.
                if (!hubs_.count(n)) promoted_[n] = 2;
                continue;
            }
        }
        if (kind & kKindCalls) {
            const auto& calls = m_.uncertainCallDrivers.at(n);
            v.uncertainCalls.insert(v.uncertainCalls.end(), calls.begin(), calls.end());
        }
        if (input) {
            if (!logic || sourceStamp_[n] != generation) {
                if (logic) sourceStamp_[n] = generation;
                v.inputs.push_back({n, logic});
            }
            continue;
        }
        if (kind & kKindBlackBox) v.bbLeaves.push_back(n);
        if (kind & kKindTruncated) v.unresolved.push_back(n);
        if (kind & kKindUndriven) v.undriven.push_back(n);
        for (auto ei : m_.in[n]) {
            auto& e = m_.edges[ei];
            if (e.type == EdgeType::Comb || e.type == EdgeType::Seq) st.push_back({e.from, logic || !e.exact});
        }
    }
    if (m_.multiSequential.count(d.q)) v.unresolved.push_back(d.q);
    if (auto it = m_.uncertainCallDrivers.find(d.q); it != m_.uncertainCallDrivers.end())
        v.uncertainCalls.insert(v.uncertainCalls.end(), it->second.begin(), it->second.end());
    const auto bySource = [](const ConeSrc& a, const ConeSrc& b) { return a.q != b.q ? a.q < b.q : a.logic < b.logic; };
    std::sort(v.flops.begin(), v.flops.end(), bySource);
    std::sort(v.inputs.begin(), v.inputs.end(), bySource);
    for (auto* nodes : {&v.bbLeaves, &v.unresolved, &v.undriven, &v.uncertainCalls, &v.skippedClasses}) {
        std::sort(nodes->begin(), nodes->end());
        nodes->erase(std::unique(nodes->begin(), nodes->end()), nodes->end());
    }
    viewListedSources_ += v.flops.size() + v.inputs.size();
    return v;
}

// The walk from `n` that `hubCone` makes, restricted to what it records besides register sources, and to the nodes that reach any of it.
const ConeFinder::OpenCone& ConeFinder::openConeOf(NodeId n, const SourceClasses& cl) {
    if (auto known = openCones_.find(n); known != openCones_.end()) { ++openConeHits_; return known->second; }
    ++openConeWalks_;
    if (openStamp_.size() != m_.nodes.size()) openStamp_.assign(m_.nodes.size(), 0);
    if (++openGen_ == 0) { std::fill(openStamp_.begin(), openStamp_.end(), 0); openGen_ = 1; }
    const uint32_t generation = openGen_;
    OpenCone c;
    const auto merge = [&](const auto& other) {
        c.inputs.insert(c.inputs.end(), other.inputs.begin(), other.inputs.end());
        c.bbLeaves.insert(c.bbLeaves.end(), other.bbLeaves.begin(), other.bbLeaves.end());
        c.unresolved.insert(c.unresolved.end(), other.unresolved.begin(), other.unresolved.end());
        c.undriven.insert(c.undriven.end(), other.undriven.begin(), other.undriven.end());
        c.uncertainCalls.insert(c.uncertainCalls.end(), other.uncertainCalls.begin(), other.uncertainCalls.end());
    };
    std::vector<NodeId> pending{n};
    openStamp_[n] = generation;
    while (!pending.empty()) {
        const NodeId x = pending.back();
        pending.pop_back();
        if (x != n) {
            if (auto known = openCones_.find(x); known != openCones_.end()) { merge(known->second); continue; }
        }
        const uint8_t kind = kind_[x];
        if (!m_.nodes[x].sym) {
            if (auto known = hubs_.find(x); known != hubs_.end()) {
                const Cone& hub = known->second;
                for (const auto& source : hub.inputs) c.inputs.push_back(source.q);
                c.bbLeaves.insert(c.bbLeaves.end(), hub.bbLeaves.begin(), hub.bbLeaves.end());
                c.unresolved.insert(c.unresolved.end(), hub.unresolved.begin(), hub.unresolved.end());
                c.undriven.insert(c.undriven.end(), hub.undriven.begin(), hub.undriven.end());
                c.uncertainCalls.insert(c.uncertainCalls.end(), hub.uncertainCalls.begin(), hub.uncertainCalls.end());
                continue;
            }
        } else {
            if (kind & kKindFlop) {   // a leaf, as in visitLeaf
                if (kind & kKindMultiSequential) c.unresolved.push_back(x);
                continue;
            }
            if (kind & kKindCalls) {
                const auto& calls = m_.uncertainCallDrivers.at(x);
                c.uncertainCalls.insert(c.uncertainCalls.end(), calls.begin(), calls.end());
            }
            if (kind & kKindInput) { c.inputs.push_back(x); continue; }
            if (kind & kKindBlackBox) c.bbLeaves.push_back(x);
            if (kind & kKindTruncated) c.unresolved.push_back(x);
            if (kind & kKindUndriven) c.undriven.push_back(x);
        }
        for (auto ei : m_.in[x]) {
            const auto& e = m_.edges[ei];
            if ((e.type == EdgeType::Comb || e.type == EdgeType::Seq) && cl.open(e.from) && openStamp_[e.from] != generation) {
                openStamp_[e.from] = generation;
                pending.push_back(e.from);
            }
        }
    }
    for (auto* nodes : {&c.inputs, &c.bbLeaves, &c.unresolved, &c.undriven, &c.uncertainCalls}) {
        std::sort(nodes->begin(), nodes->end());
        nodes->erase(std::unique(nodes->begin(), nodes->end()), nodes->end());
        nodes->shrink_to_fit();
    }
    const size_t bytes = (c.inputs.capacity() + c.bbLeaves.capacity() + c.unresolved.capacity() + c.undriven.capacity() +
                          c.uncertainCalls.capacity()) * sizeof(uint32_t) + sizeof(OpenCone);
    if (bytes > kOpenConeBudgetBytes - openConeBytes_) { ++openConeMisses_; uncachedOpenCone_ = std::move(c); return uncachedOpenCone_; }
    openConeBytes_ += bytes;
    return openCones_[n] = std::move(c);
}

void ConeFinder::releaseCaches() {
    const auto drop = [](auto& container) { container = std::decay_t<decltype(container)>(); };
    drop(byRegister_); drop(interned_); drop(hubs_); drop(filteredHubs_); drop(openCones_);
    drop(uncachedHub_); drop(uncachedOpenCone_); drop(memoStamp_); drop(memoAnswer_);
    std::fill(walkedThrough_.begin(), walkedThrough_.end(), 0);
    std::fill(promoted_.begin(), promoted_.end(), 0);   // a promoted node's stored cone is gone
    storedConeBytes_ = storedHubBytes_ = storedPromotedBytes_ = openConeBytes_ = 0;
    hubBudgetRefused_ = false;
    memoClasses_ = 0;   // the skip answers and filtered hubs are rebuilt on the next view
}

ConeFinder& sharedCones(Model& m) {
    if (!m.cones) m.cones = std::make_shared<ConeFinder>(m);
    return *m.cones;
}

} // namespace sverity::model
