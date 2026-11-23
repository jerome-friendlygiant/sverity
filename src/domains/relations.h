// Clock relationships derived from constraints (SDC subset) for the clock domains of a Model.
// Unknown relationships stay Unknown: they are never promoted to "same" or "safe".
#pragma once
#include <string>
#include <vector>

#include "domains/model.h"
#include "sverity/rule.h"

namespace sverity::model {

enum class Relation { Same, Async, Exclusive, Unknown };
const char* relName(Relation r);

class ClockRelations {
public:
    ClockRelations(const Model& m, const Config& cfg);
    /// SDC clock name a domain is constrained as ("" = unconstrained).
    const std::string& clockName(uint32_t domain) const { return names_[domain]; }
    bool hasMultipleClocks(uint32_t domain) const { return multipleClocks_[domain]; }
    Relation relation(uint32_t a, uint32_t b) const;
    /// Relationship between a model domain and an explicitly named SDC clock,
    /// preserving ambiguity on the modelled root before comparing names.
    Relation relationToName(uint32_t domain, const std::string& clock) const {
        return hasMultipleClocks(domain) ? Relation::Unknown : relationByNames(names_[domain], clock);
    }
    /// Relationship of two SDC clock names ("" = unconstrained -> Unknown).
    Relation relationByNames(const std::string& a, const std::string& b, bool requireConsistent = false) const;
    /// True when any set_clock_groups declaration separates domains `a` and `b`, overlaps ambiguously on either,
    /// or contains an unevaluated `-regexp` selector.
    bool hasClockGroupCut(uint32_t a, uint32_t b) const;
    /// SDC clock a node is declared as the source of ("" = none).
    std::string clockNameOfNode(NodeId n) const { return sdcClockForNode(n); }
    /// Whether a clock is declared on this node or, by a bit-select such as `clk[0]`, on this bit.
    bool isDeclaredClock(NodeId n) const;
    /// `get_ports` = top-level ports only; `get_pins/get_nets` = hierarchical names relative to the top instance.
    bool nameMatches(const std::string& kind, const std::string& pattern, NodeId n, const std::string& full) const;
    /// Whether a clock selector is supported and matches this modeled root name.
    bool clockSourceMatches(const sdc::ObjectRef& source, NodeId n, const std::string& full) const;
    /// False only when neither nameMatches nor bitMatches can hold for this node, without building a name: every name of a node ends
    /// with its symbol's name, optionally followed by one bit index, and a pattern without `*` or `?` must equal the whole name.
    bool mayNameMatch(const std::string& kind, const std::string& pattern, NodeId n) const;
    /// As nameMatches, against the node's own bit (`mode[1]`) rather than its whole symbol.
    bool bitMatches(const std::string& kind, const std::string& pattern, NodeId n, std::string_view rtlBitName = {}) const;
    /// Declared RTL bit name of a scalar or simple bit-vector node (handles ascending and nonzero-based ranges).
    std::string rtlBitName(NodeId node) const;
    /// Declared RTL bit name of a top-level input node (for set_input_delay and boundary_events).
    std::string boundaryInputName(NodeId input) const;
    /// True when `ref` is a supported input-delay selector matching `node`.
    bool inputDelayMatches(const sdc::ObjectRef& ref, NodeId node) const;

    struct ActiveInputContract {
        const sdc::InputDelay* delay = nullptr;
        unsigned modes = 0;
    };
    /// Effective valid `set_input_delay` contracts matching `node` per min/max mode.
    /// Sets `*rejectedMode` when any matching declaration is invalid and has not been
    /// replaced on all affected modes by a later valid non-additive declaration.
    std::vector<ActiveInputContract> effectiveInputDelays(NodeId node, bool* rejectedMode = nullptr) const;

private:
    struct InputDelayDeclaration {
        const sdc::InputDelay* delay = nullptr; // nullptr for an ordered parse/control-flow rejected event
        bool valid = false;
        bool unboundedTargets = false;          // targets cannot be bounded to specific top-level ports
    };
    std::string sdcClockForNode(NodeId n, bool* multipleClocks = nullptr) const;
    const Model& m_;
    const Config& cfg_;
    std::vector<std::string> names_;
    Relation computeRelation(uint32_t a, uint32_t b) const;
    static constexpr size_t kDenseRelationDomains = 2048;
    mutable std::vector<int8_t> relationOf_;   // domains x domains, -1 until asked: a relation depends only on the pair
    mutable std::unordered_map<uint64_t, Relation> sparseRelationOf_;   // the same, for designs with more domains
    std::vector<bool> multipleClocks_;
    std::vector<InputDelayDeclaration> inputContracts_;
};

/// The clock domains of a resolved register: its first block's, then those of any further sequential blocks.
std::vector<uint32_t> domainsOf(const Flop& f);
/// The launch/capture domain pair that is least safe (Async, then Unknown, then Exclusive, then Same), so a register with
/// several clocks is judged the same whichever block comes first. Ties take the smallest domain indices.
struct DomainPair { uint32_t launch = 0, capture = 0; Relation relation = Relation::Same; };
DomainPair worstDomains(const Flop& launch, const Flop& capture, const ClockRelations& relations);

/// Name without a trailing "[bit]" suffix.
std::string stripBit(std::string s);

} // namespace sverity::model
