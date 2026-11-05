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
    Relation relation(uint32_t a, uint32_t b) const;
    /// Relationship of two SDC clock names ("" = unconstrained -> Unknown).
    Relation relationByNames(const std::string& a, const std::string& b) const;
    /// SDC clock a node is declared as the source of ("" = none).
    std::string clockNameOfNode(NodeId n) const { return sdcClockForNode(n); }
    /// Whether a clock is declared on this node or, by a bit-select such as `clk[0]`, on this bit.
    bool isDeclaredClock(NodeId n) const;
    /// `get_ports` = top-level ports only; `get_pins/get_nets` = hierarchical names relative to the top instance.
    bool nameMatches(const std::string& kind, const std::string& pattern, NodeId n, const std::string& full) const;
    /// As nameMatches, against the node's own bit (`mode[1]`) rather than its whole symbol.
    bool bitMatches(const std::string& kind, const std::string& pattern, NodeId n) const;

private:
    std::string sdcClockForNode(NodeId n) const;
    const Model& m_;
    const Config& cfg_;
    std::vector<std::string> names_;
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
