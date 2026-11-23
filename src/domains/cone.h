// Register fan-in cones over the shared Model: which registers / primary inputs / unresolved sources feed a
// register's next-state function. Used by the CDC and RDC analyses; one implementation, no copies.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

/// The register sources of every node sorted into the classes one pass tells apart (ConeFinder::classify). A pass that reads
/// only a few sources of each cone (the ones in another domain, say) uses them to step over the parts of a cone where no source
/// can matter to it, instead of listing millions of sources it would ignore.
class SourceClasses {
public:
    static constexpr uint32_t kIgnore = UINT32_MAX - 1;   // the pass never looks at this register as a source
    static constexpr uint32_t kListed = UINT32_MAX;       // the pass needs to see this register by identity wherever it is
    static constexpr uint32_t kWide = UINT32_MAX;         // set id: a listed register or too many classes
    /// The classes of set `id` (sorted). Not for kWide.
    const std::vector<uint32_t>& classes(uint32_t id) const { return sets_[id]; }
    uint32_t setOf(NodeId n) const { return setOf_[n]; }
    /// Complete classes only: whether the walk from `n` reaches a top input or an uncertainty, which a view must list.
    bool open(NodeId n) const { return complete_ && open_[n]; }
    uint32_t classOfFlop(uint32_t flop) const { return flopClass_[flop]; }
    /// The set holding only the class of register `flop`: 0 (empty) for kIgnore, kWide for kListed.
    uint32_t singletonOfFlop(uint32_t flop) const { return flopSet_[flop]; }
    bool complete() const { return complete_; }
    size_t bytes() const;

private:
    friend class ConeFinder;
    bool complete_ = false;
    uint64_t serial_ = 0;                          // which classification this is (ConeFinder::view remembers answers per one)
    std::vector<uint32_t> flopClass_, flopSet_;    // per register (Model::flops index): class, and its singleton set
    std::vector<uint32_t> setOf_;                  // per node: what the walk from it reaches (it included), as a class set id
    std::vector<std::vector<uint32_t>> sets_;      // id -> sorted classes; 0 is the empty set
    std::vector<NodeId> fewA_, fewB_;              // per node: the first two distinct registers it reaches (kNoNode: none); fewB_ kMany: more
    std::vector<uint8_t> open_;                    // per node (complete only): reaches a top input or an uncertainty
};

/// A register's cone with the parts one pass does not need left out (ConeFinder::view). Every source a `skip` judgement does not
/// accept is listed exactly as `ConeFinder::of` lists it; the skipped registers are only summarised. With classes built
/// `complete`, the top inputs and every uncertainty list are complete too; otherwise they are empty and must not be read.
struct ConeView {
    std::vector<ConeSrc> flops, inputs;
    std::vector<NodeId> bbLeaves, unresolved, undriven;
    std::vector<uint32_t> uncertainCalls;
    std::vector<uint32_t> skippedClasses;          // sorted, unique: the classes of the registers left out
    NodeId skippedA = kNoNode, skippedB = kNoNode; // up to two distinct registers left out (the register itself included); see skippedMany
    bool skippedMany = false;                      // more than two were left out
    static constexpr NodeId kMany = kNoNode - 1;
};

class ConeFinder {
public:
    static constexpr size_t kDefaultConeBudgetBytes = size_t{1} << 31;
    static constexpr size_t kDefaultHubBudgetBytes = size_t{1} << 29;
    explicit ConeFinder(const Model& m, size_t coneBudgetBytes = kDefaultConeBudgetBytes,
                        size_t hubBudgetBytes = kDefaultHubBudgetBytes)
        : m_(m), coneBudgetBytes_(coneBudgetBytes), hubBudgetBytes_(hubBudgetBytes), s0_(m.nodes.size(), 0), s1_(m.nodes.size(), 0),
          sourceStamp_(m.nodes.size(), 0), hubSourceStamp_(m.nodes.size(), 0), hubSeenStamp_(m.nodes.size(), 0) { buildKinds(); }
    /// Fan-in cone of register `d` (control and data edges, through combinational logic, stopping at registers).
    /// Computed once per register and shared between bits with equal cones, so every rule of a run can ask freely. Past a memory
    /// budget a cone is recomputed when asked for instead of being stored: slower, same answer.
    std::shared_ptr<const Cone> of(const Flop& d);
    /// For every register whose cone holds some uncertainty, the uncertainty of the cone `of` would return, in register order. It stores
    /// no cone and no hub: a mask of four bits per node is propagated forward once, and only registers whose cone holds an unresolved
    /// call are walked, over the nodes that can reach one. The mask is kept for `undrivenOf`.
    std::vector<std::pair<uint32_t, ConeUncertainty>> uncertaintyOfRegisters();
    /// Whether the cone `of` would return for register `flop` (index in Model::flops) holds an unresolved call. Answered from one
    /// forward pass over the graph (uncertaintyOfRegisters), computed on first use; no cone is built.
    bool hasUncertainCalls(uint32_t flop);
    /// The union of the undriven nets of the cones of these registers (sorted). Needs `uncertaintyOfRegisters` first.
    std::vector<NodeId> undrivenOf(const std::vector<uint32_t>& registers) const;
    /// Fan-in cone of arbitrary nets (for example the nets wired to an input of a black box).
    Cone ofNodes(const std::vector<NodeId>& nets);
    /// Sorts every register into a pass's class (`classOf(flop index)`, or SourceClasses::kIgnore / kListed) and propagates the
    /// sets forward once, along the edges a cone walk follows. `complete`: a node that reaches a top input or holds or reaches an
    /// uncertainty (black box, truncation, undriven net, unresolved call, a register of several blocks) is never skipped, so a view
    /// lists them all. Classes are small integers; a set of more than 64 is kWide.
    template <class ClassOf>
    SourceClasses classify(ClassOf classOf, bool complete) {
        std::vector<uint32_t> classes(m_.flops.size());
        for (uint32_t fi = 0; fi < m_.flops.size(); ++fi) classes[fi] = classOf(fi);
        return classify(std::move(classes), complete);
    }
    SourceClasses classify(std::vector<uint32_t> flopClass, bool complete);
    /// The cone of `d` without the parts `skip` accepts. `skip(class set id)` is asked for the sets of whole subgraphs and of single
    /// registers (a singleton set); it is never asked for kWide. It must accept a set only when it accepts every class in it (so
    /// every subset too), and its answer must depend only on the set and on `key`: answers are remembered until a view asks with
    /// other classes or another key. A logic hub whose registers are all accepted but which reaches an input or uncertainty adds
    /// those from its stored cone without listing the registers. A skipped subgraph or register is not walked or listed; its
    /// classes and its first two registers are added to the view's summary. No cone is stored.
    template <class Skip>
    ConeView view(const Flop& d, const SourceClasses& classes, uint64_t key, Skip&& skip) {
        return viewImpl(d, classes, key, [&](uint32_t id) { return static_cast<bool>(skip(id)); });
    }
    /// Whether node `n` is the output of a register (Model::flopOfQ, without the hash lookup).
    bool isRegister(NodeId n) const { return kind_[n] & kKindFlop; }
    uint32_t flopIndexOf(NodeId q) const { return flopIndex_[q]; }   // index in Model::flops of a register output
    /// Drops every stored cone, hub and view cache (later requests recompute them: slower, same answers).
    void releaseCaches();
    /// Bounded counters for the optional timings file; no per-cone history is retained.
    std::vector<std::pair<std::string, uint64_t>> telemetry() const;

private:
    friend struct ConeFinderTestAccess;
    size_t retainedConeBytes() const { return storedConeBytes_; }
    size_t retainedHubBytes() const { return storedHubBytes_; }
    Cone walk(std::vector<std::pair<NodeId, bool>> seeds, NodeId self);
    void buildKinds();
    enum : uint8_t { kKindFlop = 1, kKindMultiSequential = 2, kKindCalls = 4, kKindInput = 8, kKindBlackBox = 16, kKindTruncated = 32,
                     kKindUndriven = 64 };
    std::vector<uint8_t> kind_;          // per node: what a walk records or stops at (buildKinds)
    std::vector<uint32_t> flopIndex_;    // per node: index in Model::flops, or UINT32_MAX
    ConeView viewImpl(const Flop& d, const SourceClasses& classes, uint64_t key, const std::function<bool(uint32_t)>& skip);
    // Remembered `skip` answers per class set, valid while the classification (memoClasses_) and memoKey_ stay the same.
    uint64_t memoClasses_ = 0, classifications_ = 0;
    uint64_t memoKey_ = 0;
    uint32_t memoRound_ = 0;
    std::vector<uint32_t> memoStamp_;
    std::vector<uint8_t> memoAnswer_;
    // A hub's register sources filtered under the remembered answers: the ones listed, and a summary of the ones skipped. Cleared
    // with the answers, so consecutive registers of one domain filter each hub once.
    struct FilteredHub {
        std::vector<NodeId> listed;
        std::vector<uint32_t> skippedSets;
        NodeId skippedA = kNoNode, skippedB = kNoNode;
        bool skippedMany = false;
    };
    std::unordered_map<NodeId, FilteredHub> filteredHubs_;
    // What a walk reaching a node through logic must list besides registers: its top inputs and uncertainty. It depends only on the
    // graph, so it is kept across views and passes, within its own budget (past it, recomputed: slower, same answer).
    struct OpenCone {
        std::vector<NodeId> inputs, bbLeaves, unresolved, undriven;
        std::vector<uint32_t> uncertainCalls;
    };
    static constexpr size_t kOpenConeBudgetBytes = size_t{1} << 28;
    std::unordered_map<NodeId, OpenCone> openCones_;
    OpenCone uncachedOpenCone_;
    size_t openConeBytes_ = 0;
    uint64_t openConeWalks_ = 0, openConeHits_ = 0, openConeMisses_ = 0;
    std::vector<uint32_t> openStamp_;
    // Named nodes walked through logic by several views become hubs (viewImpl); counted per node, saturating.
    static constexpr uint8_t kPromoteAfter = 4;
    static constexpr size_t kPromotedBudgetBytes = size_t{1} << 29;
    size_t storedPromotedBytes_ = 0;
    bool hubBudgetRefused_ = false;          // a logic hub's cone did not fit: views walk through hubs not already stored
    uint64_t viewHubsWalkedThrough_ = 0;
    std::vector<uint8_t> walkedThrough_, promoted_;   // promoted_: 0 no, 1 a hub now, 2 its cone did not fit the hub budget
    uint64_t viewPromotedNodes_ = 0;
    uint32_t openGen_ = 0;
    const OpenCone& openConeOf(NodeId n, const SourceClasses& classes);
    const Cone& hubCone(NodeId hub);
    bool visitLeaf(NodeId n, Cone& c, uint32_t generation);
    void mergeHub(const Cone& hub, NodeId self, Cone& c, std::vector<uint32_t>& stamps, uint32_t generation);
    std::unordered_map<NodeId, Cone> hubs_;
    Cone uncachedHub_;
    const Model& m_;
    size_t coneBudgetBytes_, hubBudgetBytes_;
    std::unordered_map<NodeId, std::shared_ptr<const Cone>> byRegister_;
    std::unordered_map<size_t, std::vector<std::shared_ptr<const Cone>>> interned_;   // by content hash
    size_t storedConeBytes_ = 0, storedHubBytes_ = 0;
    uint64_t coneRequests_ = 0, coneRegisterCacheHits_ = 0, coneWalks_ = 0, coneInternHits_ = 0, coneBudgetMisses_ = 0;
    uint64_t hubCacheHits_ = 0, hubWalks_ = 0, hubBudgetMisses_ = 0, coneSourceItems_ = 0, hubSourceItems_ = 0;
    uint64_t coneCompactedBytes_ = 0, hubCompactedBytes_ = 0;
    uint64_t coneViews_ = 0, viewSkippedSubgraphs_ = 0, viewListedSources_ = 0, viewVisitedNodes_ = 0, viewOpenNodes_ = 0;
    uint64_t viewHubMerges_ = 0, viewHubSources_ = 0;
    size_t maxConePayloadBytes_ = 0, maxHubPayloadBytes_ = 0, maxConeSources_ = 0, maxHubSources_ = 0;
    std::vector<uint32_t> s0_, s1_;
    // Reuse node-indexed generation stamps instead of allocating hash sets for every cone/hub source list.
    std::vector<uint32_t> sourceStamp_;
    // A cold hub walk is nested inside an outer walk and cannot reuse its stamps.
    std::vector<uint32_t> hubSourceStamp_, hubSeenStamp_;
    std::vector<uint8_t> up_;   // see uncertaintyOfRegisters
    std::vector<uint8_t> callsOfRegister_;   // see hasUncertainCalls; empty until first asked
    uint32_t gen_ = 0, hubGen_ = 0;
};

/// The run's one finder for this model: every family shares what it has already computed.
ConeFinder& sharedCones(Model& m);

} // namespace sverity::model
