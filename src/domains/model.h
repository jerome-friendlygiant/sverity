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
    const slang::ast::ValueSymbol* sym = nullptr;        // null for a logic hub: many sources reaching many targets through one node
    uint32_t bit = 0;                                    // bit offset within sym (0 for collapsed symbols)
};

/// The operator the source participates in on its way to the target (innermost operator around the source's reference).
/// None = a pure bit move (alias, concatenation, select, extension). Mux = data arm of ?:, MuxSel = its select.
enum class Op : uint8_t { None, And, Or, Xor, Not, Mux, MuxSel, Compare, Arith, Shift, Select, Other };

/// Where the source sits in the control structure of the assignment (for ctrl edges).
enum class Branch : uint8_t { None, Taken, NotTaken };

// Large designs hold about 100 million of these, so the flags are packed.
struct Edge {
    NodeId from = kNoNode, to = kNoNode;
    uint32_t proc = 0;                                   // index into Model::procs
    EdgeType type : 2 = EdgeType::Comb;
    bool exact : 1 = false;
    bool ctrl : 1 = false;                               // the source is a control-flow condition (enable/select), not data
    Op op : 4 = Op::None;                                // innermost operator on the source's path (Other when it differs between uses)
    uint8_t inv : 2 = 0;                                 // 0 the source reaches the target true-polarity, 1 inverted (odd number of NOT /
                                                         // XNOR/NAND/NOR/!= on the way), 2 unknown (arithmetic, comparison, mixed uses)
    Branch branch : 2 = Branch::None;                    // ctrl edges: the assignment runs when the condition holds / does not hold
    bool approximate : 1 = false;                      // from an assignment with a computed index or a user call: all-to-all may be far more than is real
    bool internalRead : 1 = false;                       // Comb: Flow proved this bit is never read at its pre-activation value in `proc`,
                                                         // so the source is the value `proc` itself wrote earlier, not feedback
};
static_assert(sizeof(Edge) == 16);

/// Edge indices per node, in one contiguous block.
class Adjacency {
public:
    struct Span {
        const uint32_t* first; const uint32_t* last;
        const uint32_t* begin() const { return first; }
        const uint32_t* end() const { return last; }
        size_t size() const { return static_cast<size_t>(last - first); }
        bool empty() const { return first == last; }
        uint32_t operator[](size_t i) const { return first[i]; }
    };
    Span operator[](NodeId n) const { return {data_.data() + offsets_[n], data_.data() + offsets_[n + 1]}; }
    size_t size() const { return offsets_.empty() ? 0 : offsets_.size() - 1; }
    template<class NodeOfEdge>
    void build(size_t nodes, size_t edges, NodeOfEdge nodeOf) {
        offsets_.assign(nodes + 1, 0);
        for (size_t e = 0; e < edges; e++) offsets_[nodeOf(e) + 1]++;
        for (size_t n = 0; n < nodes; n++) offsets_[n + 1] += offsets_[n];
        data_.resize(edges);
        std::vector<uint32_t> next(offsets_.begin(), offsets_.end() - 1);
        for (size_t e = 0; e < edges; e++) data_[next[nodeOf(e)]++] = static_cast<uint32_t>(e);
    }

private:
    std::vector<uint32_t> offsets_, data_;
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
    std::vector<int> resetKinds;                         // per reset: value it forces on this register bit: 0 clear, 1 set, 2 unknown (derived from resetValues)
    std::vector<char> resetValues;                       // per reset: the constant this bit gets: '0' '1' 'x' 'z', '?' (assigned a non-constant or conflicting value), 'n' (not assigned by that reset's branch)

    // Exact two-branch asynchronous transition (one unconditional NBA per bit in each branch).
    bool resetTransitionKnown = false;
    NodeId releasedSource = kNoNode;                     // exact normal D, or releasedValue for a literal
    char releasedValue = '?';                           // X/Z retained, never a binary-value proof

    bool hasSyncResetCandidate = false;                  // heuristic: reset-first conditional or final constant override
    int extraStages = 0;                                 // library synchroniser cell: stages beyond this register
    bool blocking = false;                               // assigned with a blocking assignment (never a synchroniser stage)
    bool syncActiveLow = false;                          // polarity of the synchronous reset candidate (condition is a negation)
    NodeId syncReset = kNoNode;                          // the candidate's signal (control edges from it are ignored for chain recognition)
    uint32_t domain = 0;                                 // index into Model::domains (valid if clockResolved)
    std::vector<NodeId> otherClockSignals;               // clocks of further sequential blocks driving this bit (multiSequential)
    std::vector<uint32_t> otherDomains;                  // their domains; rules must weigh all of them, independently of source order
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

enum class PortRole : uint8_t { Unknown, Clock, Reset, Enable, DataIn, DataOut, Address, WriteEnable, GatedClock };
enum class PortDir : uint8_t { Unknown, In, Out, InOut };

/// One connection of a black-box (or library) instance. `dir`/`role` are Unknown unless the --library declaration names them.
struct BlackBoxPort {
    std::string name;
    PortDir dir = PortDir::Unknown;
    PortRole role = PortRole::Unknown;
    std::string clock;                                   // library: name of the clock port this port is synchronous to ("" = not declared)
    std::vector<NodeId> nodes;                           // the actual signal's bits in the instantiating scope
};

struct BlackBoxInst {
    std::string module;
    std::string hierarchy;
    SourceLocation loc;
    std::vector<BlackBoxPort> ports;
    bool declared = false;                               // a --library entry describes the ports (roles and directions known)
};

struct LibInstance {
    std::string module, hierarchy;
    std::string kind;                                    // synchronizer | flop | clock_gate | ram
    int stages = 2;
    SourceLocation loc;
    std::vector<BlackBoxPort> ports;                     // every declared port with the nodes connected to it
};

/// A library clock gate: `out` is the gated copy of `clk`, switched by `enable` (clock tracing follows out -> clk).
struct ClockGateInst {
    std::string module, hierarchy;
    std::vector<NodeId> clk, enable, out;
    bool latched = false;                                // the library declares the enable captured glitch-free inside the cell
    SourceLocation loc;
};

/// A library RAM: written in the `writeClk` domain, read in the `readClk` domain (or asynchronously). Read data nodes are
/// blackBoxDriven == 2.
struct RamInst {
    std::string module, hierarchy;
    std::vector<NodeId> writeClk, readClk;
    std::vector<BlackBoxPort> ports;
    bool syncRead = true;
    SourceLocation loc;
    int64_t writeDomain = -1, readDomain = -1;           // index into Model::domains of the write / read clock (-1 = not resolved), set by finish()
};

/// A constant bit that drives a node through an exact move (`assign y = 1'bx;`, `q <= 4'b0001`). value: '0' '1' 'x' 'z'.
struct ConstDriver {
    char value = '?';
    EdgeType type = EdgeType::Comb;
    uint32_t proc = 0;
};

// Boolean syntax facts for qualifier implication. Unknown retains dependencies;
// it never asserts Boolean structure for a case, call, or unsupported operator.
struct BoolFact {
    enum class Kind { Constant, Node, And, Or, Not, Unknown };
    Kind kind = Kind::Unknown;
    NodeId node = kNoNode;
    bool value = false;
    bool opaque = false;
    std::vector<std::shared_ptr<const BoolFact>> args;
};
using BoolFactPtr = std::shared_ptr<const BoolFact>;
struct GuardCondition {
    BoolFactPtr predicate;
    std::vector<NodeId> dependencies;
};
struct BooleanDriver {
    BoolFactPtr value;
    bool unconditional = false;
};

struct CallUncertainty {
    std::string call, reason;
    SourceLocation location;
};

class ConeFinder;

class Model {
public:
    std::shared_ptr<ConeFinder> cones;                   // see sharedCones()
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<ProcInfo> procs;
    std::vector<Flop> flops;
    std::vector<Domain> domains;
    std::vector<Fallback> fallbacks;
    std::vector<CallUncertainty> callUncertainties;
    std::unordered_map<NodeId, std::vector<uint32_t>> uncertainCallDrivers;
    std::unordered_map<NodeId, char> modeConstants;
    uint64_t casePrunedBranches = 0, casePrunedExpressions = 0;
    uint64_t subroutineSummaries = 0, subroutineBodyNodes = 0;
    std::vector<BlackBoxInst> blackBoxes;
    std::vector<LibInstance> libInstances;               // instances of declared library cells (every kind)
    std::unordered_map<NodeId, std::vector<ConstDriver>> constDrivers;   // exact constant drivers per node (X/Z kept)
    std::vector<ClockGateInst> clockGates;
    std::unordered_set<const slang::ast::ValueSymbol*> loopVariables;   // procedural loop indices: no driver, and not design state
    std::vector<RamInst> rams;

    // adjacency (edge indices), built by finish()
    Adjacency in, out;
    std::unordered_map<NodeId, uint32_t> flopOfQ;        // Q node -> index in flops
    std::unordered_map<NodeId, uint32_t> selfComb;       // node -> procedure of a combinational dependency on itself
    std::unordered_set<NodeId> selfCombApproximate;          // ... that exists only through an assignment with a computed index or a user call
    /// per register bit: for every data-carrying assignment, the (non-reset) control nodes guarding it; an empty list = unguarded.
    std::unordered_map<NodeId, std::vector<std::vector<NodeId>>> guards;
    std::unordered_map<NodeId, std::vector<std::vector<GuardCondition>>> qualifierGuards;
    std::unordered_map<NodeId, std::vector<BooleanDriver>> booleanDrivers;
    std::unordered_set<NodeId> selfExact;                // exact register self-assignment (a hold)
    std::unordered_set<NodeId> multiSequential;          // assigned by several sequential blocks: see Flop::otherDomains
    std::unordered_set<NodeId> selfLogic;                // registers whose next value depends on their own value through logic (toggle/counter)
    std::vector<uint8_t> blackBoxDriven;                 // per node: 1 = may be driven by an unmodelled black box, 2 = read data of a library RAM
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

    /// Exact, unconditional combinational literal behind aliases; '?' if not established (X/Z kept).
    char constantValue(NodeId n) const;

    void finish();   // builds adjacency, resolves clock roots, assigns domains
    void compactEdges();   // sorts edges and removes duplicates; building calls it rather than let the vector keep doubling
};

/// Returns the design's shared model, building it on first use.
Model& getModel(Design& design);

/// Builds the model from an elaborated design. Never throws for unsupported constructs; they become
/// `fallbacks`.
std::unique_ptr<Model> buildModel(Design& design);

} // namespace sverity::model
