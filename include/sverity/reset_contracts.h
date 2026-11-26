// Typed `reset_contracts` configuration section (RFC 1.0 structural RDC). Parsing checks schema only;
// names, reset-chain resolution and ordering invariants are checked by the CST-016 binding step.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "sverity/json.h"

namespace sverity {

struct ResetContracts {
    struct Source { std::string id, port, root, active; };                       // exactly one of port/root; active low|high
    struct Group  { std::string id; std::vector<std::string> sources; };
    struct Order  { std::string id, before, after, clock, edge; int64_t minCycles = 0; };   // edge posedge|negedge
    struct State  { std::string id, source, initial; };                          // initial == "asserted"
    bool present = false;                                                         // the section was given
    std::vector<Source> sources;
    std::vector<Group> assertTogether;
    std::vector<Order> releaseOrder;
    std::vector<State> state;
};

/// Parses the `reset_contracts` object. Returns false with a one-line message (prefixed "reset_contracts") on a schema error.
bool parseResetContracts(const json::Value& section, ResetContracts& out, std::string& error);
/// Normalized form for report inputs; keys match the input schema, empty clause arrays are omitted.
json::Value toJson(const ResetContracts& contracts);

} // namespace sverity
