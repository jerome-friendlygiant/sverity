// Binding of the `reset_contracts` section to the elaborated design (RFC 1.0 structural RDC, CST-016).
// A source names an exact RTL reset origin: a top-level input bit (`port`) or an internal node that is the immediate traced
// root of an asynchronous reset (`root`). Clauses are then bound only to bound sources. Every decision is made from exact
// names and the shared model; no name heuristic or inferred edge contributes.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "domains/model.h"
#include "domains/relations.h"
#include "sverity/diagnostic.h"
#include "sverity/json.h"
#include "sverity/reset_contracts.h"
#include "sverity/sdc.h"

namespace sverity::model {

enum class ContractKind : uint8_t { Source, Group, Order, State };
/// Bound: a source that resolved exactly (no finding of its own; its clauses carry the evidence).
/// Established: a release order the RTL satisfies by the structural witness (INFO). Conflict: a demonstrated contradiction.
enum class ContractStatus : uint8_t { Bound, Assumed, RtlRequirement, Established, Unapplied, Unresolved, Conflict };
const char* contractKindName(ContractKind k);
const char* contractStatusName(ContractStatus s);

struct ContractEvidence {
    std::string text;
    SourceLocation location;
};

struct ContractBinding {
    ContractKind kind = ContractKind::Source;
    std::string id;
    ContractStatus status = ContractStatus::Unapplied;
    std::vector<std::string> reasons;
    std::vector<std::string> bits;          // canonical RTL bit names (source: one; order: before, after; group: members)
    std::vector<std::string> sourceIds;     // source ids the clause refers to, config order
    std::string polarity;                   // source: declared polarity low|high
    std::string clock, edge;                // order
    std::string clockRoot;                  // order: the exact RTL top-level input of the clock, when the clock is valid (CST-016 B1)
    int64_t minCycles = 0;                  // order
    int64_t chainStages = 0;                // order, RTL witness: stages L of the recognised chain
    int64_t establishedEdges = -1;          // order, RTL witness: structural bound on edges (-1 when no witness matched)
    std::string stateId;                    // order, RTL witness: the initial asserted state clause that covers the raw input
    std::vector<ContractEvidence> evidence; // RTL locations of the reset uses behind a source
    bool used = false;                      // source: referenced by a clause
};

struct ContractBindingResult {
    std::vector<ContractBinding> entries;   // sources, then groups, orders and states, each in config order
};

/// Chain facts of a candidate reset synchroniser whose last stage is a register index (RDC-003 `syncChain` facts).
/// `stages` lists register indices last stage first; `first` is the constant-D stage when `recognised`.
struct SyncChainFacts {
    bool recognised = false;
    std::string why;                        // reason when not recognised
    std::vector<uint32_t> stages;
    uint32_t first = ~0u;
};
using SyncChainOf = std::function<SyncChainFacts(uint32_t lastStage)>;

/// Binds every clause of a present `reset_contracts` section. Pure: reads the model, the clock relations and the constraints.
/// `minStages` is the synchroniser depth a witness chain must reach (RDC-006.min_stages).
ContractBindingResult bindResetContracts(const Model& m, const ClockRelations& rel, const ResetContracts& contracts,
                                         const sdc::Constraints& constraints, const SyncChainOf& chainOf, int64_t minStages = 2);

/// Canonical RTL name of a node (the model's bit name, or its node name when it has none): the name clauses are matched by.
std::string contractBitName(const Model& m, const ClockRelations& rel, NodeId n);

/// Clauses that bind the RTL reset bit `bit` by exact name: a resolved source of that name, or a group, release order or state
/// clause that names it. Unapplied clauses bind nothing. Config order.
std::vector<const ContractBinding*> contractsOfBit(const ContractBindingResult& result, const std::string& bit);

/// Release-order clauses between the RTL reset bits `a` and `b` (either direction) that are not Unapplied. Config order.
std::vector<const ContractBinding*> ordersBetween(const ContractBindingResult& result, const std::string& a, const std::string& b);

/// Report input `reset_contract_validation`: one object per entry with its status, reasons, bits and evidence.
json::Value bindingsToJson(const ContractBindingResult& result);

} // namespace sverity::model
