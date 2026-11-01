// Register fan-in cones over the shared Model: which registers / primary inputs / unresolved sources feed a
// register's next-state function. Used by the CDC and RDC analyses; one implementation, no copies.
#pragma once
#include <vector>

#include "domains/model.h"

namespace sverity::model {

struct ConeSrc { NodeId q; bool logic; };   // `logic`: at least one non-exact (combinational) edge on the way

struct Cone {
    std::vector<ConeSrc> flops;        // source registers
    std::vector<ConeSrc> inputs;       // top-level input bits
    std::vector<NodeId> bbLeaves;      // nets touching an unmodeled black box (may be driven by it)
    std::vector<NodeId> unresolved;    // nets whose drivers were truncated by a precision fallback
    std::vector<NodeId> undriven;      // nets with no driver at all
};

class ConeFinder {
public:
    explicit ConeFinder(const Model& m) : m_(m), s0_(m.nodes.size(), 0), s1_(m.nodes.size(), 0) {}
    /// Fan-in cone of register `d` (control and data edges, through combinational logic, stopping at registers).
    Cone of(const Flop& d);
    /// Fan-in cone of arbitrary nets (for example the nets wired to an input of a black box).
    Cone ofNodes(const std::vector<NodeId>& nets);

private:
    Cone walk(std::vector<std::pair<NodeId, bool>> seeds, NodeId self);
    const Model& m_;
    std::vector<uint32_t> s0_, s1_;
    uint32_t gen_ = 0;
};

} // namespace sverity::model
