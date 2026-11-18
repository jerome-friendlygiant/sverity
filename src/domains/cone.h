// Register fan-in cones over the shared Model: which registers / primary inputs / unresolved sources feed a
// register's next-state function. Used by the CDC and RDC analyses; one implementation, no copies.
#pragma once
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "domains/model.h"

namespace sverity::model {

struct ConeSrc {   // four bytes: a large design stores hundreds of millions
    NodeId q : 31;
    NodeId logic : 1;
    ConeSrc(NodeId node, bool viaLogic) : q(node), logic(viaLogic) {}
};   // `logic`: at least one non-exact (combinational) edge on the way

struct Cone {
    std::vector<ConeSrc> flops;        // source registers
    std::vector<ConeSrc> inputs;       // top-level input bits
    std::vector<NodeId> bbLeaves;      // nets touching an unmodeled black box (may be driven by it)
    std::vector<NodeId> unresolved;    // nets whose drivers were truncated by a precision fallback
    std::vector<uint32_t> uncertainCalls; // unresolved subroutines on this capture path (Model issue indices)
    std::vector<NodeId> undriven;      // nets with no driver at all
};

/// What a register's cone says about the reliability of its sources, without the sources themselves.
struct ConeUncertainty {
    bool blackBox = false;                 // Cone::bbLeaves is not empty
    bool unresolved = false;               // Cone::unresolved is not empty
    bool undriven = false;                 // Cone::undriven is not empty (its nets: ConeFinder::undrivenOf)
    std::vector<uint32_t> calls;           // Cone::uncertainCalls
};

class ConeFinder {
public:
    static constexpr size_t kDefaultConeBudgetBytes = size_t{1} << 31;
    static constexpr size_t kDefaultHubBudgetBytes = size_t{1} << 29;
    explicit ConeFinder(const Model& m, size_t coneBudgetBytes = kDefaultConeBudgetBytes,
                        size_t hubBudgetBytes = kDefaultHubBudgetBytes)
        : m_(m), coneBudgetBytes_(coneBudgetBytes), hubBudgetBytes_(hubBudgetBytes), s0_(m.nodes.size(), 0), s1_(m.nodes.size(), 0) {}
    /// Fan-in cone of register `d` (control and data edges, through combinational logic, stopping at registers).
    /// Computed once per register and shared between bits with equal cones, so every rule of a run can ask freely. Past a memory
    /// budget a cone is recomputed when asked for instead of being stored: slower, same answer.
    std::shared_ptr<const Cone> of(const Flop& d);
    /// For every register whose cone holds some uncertainty, the uncertainty of the cone `of` would return, in register order. It stores
    /// no cone and no hub: a mask of four bits per node is propagated forward once, and only registers whose cone holds an unresolved
    /// call are walked, over the nodes that can reach one. The mask is kept for `undrivenOf`.
    std::vector<std::pair<uint32_t, ConeUncertainty>> uncertaintyOfRegisters();
    /// The union of the undriven nets of the cones of these registers (sorted). Needs `uncertaintyOfRegisters` first.
    std::vector<NodeId> undrivenOf(const std::vector<uint32_t>& registers) const;
    /// Fan-in cone of arbitrary nets (for example the nets wired to an input of a black box).
    Cone ofNodes(const std::vector<NodeId>& nets);

private:
    friend struct ConeFinderTestAccess;
    size_t retainedConeBytes() const { return storedConeBytes_; }
    size_t retainedHubBytes() const { return storedHubBytes_; }
    Cone walk(std::vector<std::pair<NodeId, bool>> seeds, NodeId self);
    const Cone& hubCone(NodeId hub);
    bool visitLeaf(NodeId n, Cone& c, std::unordered_set<NodeId>& flops, std::unordered_set<NodeId>& inputs) const;
    void mergeHub(const Cone& hub, NodeId self, Cone& c, std::unordered_set<NodeId>& flops, std::unordered_set<NodeId>& inputs) const;
    std::unordered_map<NodeId, Cone> hubs_;
    Cone uncachedHub_;
    const Model& m_;
    size_t coneBudgetBytes_, hubBudgetBytes_;
    std::unordered_map<NodeId, std::shared_ptr<const Cone>> byRegister_;
    std::unordered_map<size_t, std::vector<std::shared_ptr<const Cone>>> interned_;   // by content hash
    size_t storedConeBytes_ = 0, storedHubBytes_ = 0;
    std::vector<uint32_t> s0_, s1_;
    std::vector<uint8_t> up_;   // see uncertaintyOfRegisters
    uint32_t gen_ = 0;
};

/// The run's one finder for this model: every family shares what it has already computed.
ConeFinder& sharedCones(Model& m);

} // namespace sverity::model
