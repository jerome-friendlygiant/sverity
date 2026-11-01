// Shared design model: bit-level dependency graph + clock/reset extraction (RFC section 2:
// ConnectivityGraph + ClockResetModel). Built once per run from the slang elaboration; every
// CDC/RDC/CST rule consumes it. Precision fallbacks are *recorded*, never silent (CST-010).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sverity/diagnostic.h"
#include "sverity/frontend.h"

namespace slang::ast {
class InstanceSymbol;
class ValueSymbol;
class Symbol;
enum class EdgeKind : int;
} // namespace slang::ast

namespace sverity::model {

using NodeId = uint32_t;
constexpr NodeId kNoNode = 0xFFFFFFFFu;

/// Comb  = combinational dependency
/// Seq   = flop D-side dependency (source influences Q on the next clock edge)
/// AsyncCtl = asynchronous reset/set pin dependency
/// An edge with `exact` set is a pure bit move (alias / plain register transfer): no logic on the path.
enum class EdgeType : uint8_t { Comb, Seq, AsyncCtl };

struct Node {
    const slang::ast::InstanceSymbol* inst = nullptr;   // instance context (shared bodies are per-instance)
    const slang::ast::ValueSymbol* sym = nullptr;
    uint32_t bit = 0;                                    // bit offset within sym (0 for collapsed symbols)
};

struct Edge {
    NodeId from = kNoNode, to = kNoNode;
    EdgeType type = EdgeType::Comb;
    bool exact = false;
    uint32_t proc = 0;                                   // index into Model::procs
    bool ctrl = false;                                   // the source is a control-flow condition (enable/select), not data
};

enum class ProcKind : uint8_t { Continuous, Comb, Latch, Sequential, PortConnection, Other };

struct ProcInfo {
    ProcKind kind = ProcKind::Other;
    SourceLocation loc;
    std::string instancePath;
};

enum class RootKind : uint8_t { TopPort, Logic, BlackBox, Undriven, Cycle };

struct Root {
    NodeId node = kNoNode;                               // the node the trace ended at
    RootKind kind = RootKind::Undriven;
};

struct ResetInfo {
    NodeId signal = kNoNode;       // reset signal node in the register's instance
    Root root;
    bool async = true;
    bool activeLow = false;
    bool fromEdge = true;
};

struct Flop {
    NodeId q = kNoNode;
    uint32_t proc = 0;
    NodeId clockSignal = kNoNode;                        // clock signal node as seen by the register
    bool clockResolved = false;                          // a single clock event was identified
    Root clockRoot;
    int edge = 0;                                        // 1 posedge, 2 negedge, 3 both, 0 unknown
    std::vector<ResetInfo> resets;                       // async resets (from event list)
    std::vector<int> resetKinds;                         // per reset: value it forces on this register: 0 clear, 1 set, 2 unknown

    bool hasSyncResetCandidate = false;                  // heuristic: top-level if(!x) q<=const
    int extraStages = 0;                                 // library synchroniser cell: stages beyond this register
    bool blocking = false;                               // assigned with a blocking assignment (never a synchroniser stage)
    bool syncActiveLow = false;                          // polarity of the synchronous reset candidate (condition is a negation)
    NodeId syncReset = kNoNode;                          // the candidate's signal (control edges from it are ignored for chain recognition)
    uint32_t domain = 0;                                 // index into Model::domains (valid if clockResolved)
};

struct Domain {
    NodeId root = kNoNode;
    RootKind kind = RootKind::Undriven;
    std::string name;                                    // hierarchical name of the root signal
};

struct Fallback {
    std::string category;      // e.g. "wide-symbol", "interface-port", "user-function-call", "ref-port"
    std::string detail;
    SourceLocation loc;
};

struct LibInstance {
    std::string module, hierarchy;
    int stages = 2;
    SourceLocation loc;
};

struct BlackBoxInst {
    std::string module;
    std::string hierarchy;
    SourceLocation loc;
};

class Model {
public:
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<ProcInfo> procs;
    std::vector<Flop> flops;
    std::vector<Domain> domains;
    std::vector<Fallback> fallbacks;
    std::vector<BlackBoxInst> blackBoxes;
    std::vector<LibInstance> libInstances;               // instances of declared library synchroniser cells

    // adjacency (edge indices), built by finish()
    std::vector<std::vector<uint32_t>> in, out;
    std::unordered_map<NodeId, uint32_t> flopOfQ;        // Q node -> index in flops
    std::unordered_map<NodeId, uint32_t> selfComb;       // node -> procedure of a combinational dependency on itself
    /// per register bit: for every data-carrying assignment, the (non-reset) control nodes guarding it; an empty list = unguarded.
    std::unordered_map<NodeId, std::vector<std::vector<NodeId>>> guards;
    std::unordered_set<NodeId> selfLogic;                // registers whose next value depends on their own value through logic (toggle/counter)
    std::vector<uint8_t> blackBoxDriven;                 // per node
    std::vector<uint8_t> topInput;                       // per node: input port bit of a top instance
    std::vector<uint8_t> unresolvedDriven;               // per node: drivers truncated by a precision fallback
    std::vector<uint8_t> topOutput;                      // per node: output port bit of a top instance (observable)
    uint64_t numProcedures = 0, numSeqProcedures = 0, numInstances = 0;

    std::string nameOf(NodeId n) const;
    /// Declared bit width of the node's symbol (0 if unknown).
    uint64_t symbolWidth(NodeId n) const;
    /// True when the symbol is wider than the per-bit tracking limit, so the node stands for the whole symbol.
    bool collapsed(NodeId n) const;
    const Flop* flopAt(NodeId q) const {
        auto it = flopOfQ.find(q);
        return it == flopOfQ.end() ? nullptr : &flops[it->second];
    }
    /// Follows exact Comb (alias) edges backwards to the driving root.
    Root traceRoot(NodeId n) const;
    /// Follows single exact alias edges backwards as far as they go (the node whose value `n` is a plain copy of).
    NodeId aliasRoot(NodeId n) const;
    /// The register whose output `n` is a plain alias of, if any (`idx` receives its index in `flops`).
    const Flop* flopBehind(NodeId n, uint32_t* idx = nullptr) const;

    void finish();   // builds adjacency, resolves clock roots, assigns domains
};

/// Returns the design's shared model, building it on first use.
Model& getModel(Design& design);

/// Builds the model from an elaborated design. Never throws for unsupported constructs; they become
/// `fallbacks`.
std::unique_ptr<Model> buildModel(Design& design);

} // namespace sverity::model
