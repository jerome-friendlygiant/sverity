// Shared design model: bit-level dependency graph + clock/reset extraction (RFC section 2:
// ConnectivityGraph + ClockResetModel). Built once per run from the slang elaboration; every
// CDC/RDC/CST rule consumes it. Precision fallbacks are *recorded*, never silent (CST-010).
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sverity/diagnostic.h"
#include "sverity/frontend.h"

namespace slang::ast {
class InstanceSymbol;
class ValueSymbol;
class ProceduralBlockSymbol;
class ConditionalStatement;
class Expression;
class EvalContext;
class Statement;
class Symbol;
enum class EdgeKind : int;
} // namespace slang::ast

namespace sverity::model {

/// Selects a call-free procedural condition only when its value is fully known
/// in this elaborated evaluation context. Runtime and unknown values stay open.
std::optional<bool> elaboratedCondition(const slang::ast::Expression& expression,
                                       slang::ast::EvalContext& context);

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
    bool flip : 1 = false;                               // not exact, but a bit-for-bit copy through one inversion (`~x`, `!x` of one bit)
    bool ctrl : 1 = false;                               // the source is a control-flow condition (enable/select), not data
    Op op : 4 = Op::None;                                // innermost operator on the source's path (Other when it differs between uses)
    uint8_t inv : 2 = 0;                                 // 0 the source reaches the target true-polarity, 1 inverted (odd number of NOT /
                                                         // XNOR/NAND/NOR/!= on the way), 2 unknown (arithmetic, comparison, mixed uses)
    Branch branch : 2 = Branch::None;                    // ctrl edges: the assignment runs when the condition holds / does not hold
    bool approximate : 1 = false;                      // from an assignment with a computed index or a user call: all-to-all may be far more than is real
    bool shared : 1 = false;                             // the assignment runs under a run-time condition, or its right side reads more than one signal: the source is a copy only some of the time or beside others
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
    size_t bytes() const;   // heap bytes held (capacity)
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

struct ProceduralWrite {
    const slang::ast::ProceduralBlockSymbol* process = nullptr;
    const slang::ast::InstanceSymbol* instance = nullptr;
    const slang::ast::ValueSymbol* target = nullptr;
    SourceLocation loc;
    bool nonBlocking = false;
    bool called = false; // true only for an effect whose body or target cannot be examined
    bool fromCall = false;
    bool copyBackValueUnknown = false; // bounded obligation about the value copied out
    bool forkOwnership = false; // effect occurs in a fork child with unmodelled process identity
    bool throughCopyFormal = false; // body write to an output/inout formal, not a write to its actual
    const slang::ast::Expression* targetExpr = nullptr;
    const slang::ast::Expression* value = nullptr;   // the right-hand side of a plain (not compound) assignment
    bool exactTarget = false;
    bool constantValue = false;
    bool wholeConstant = false;
    bool partialTarget = false;
    unsigned conditionalDepth = 0;
    std::string reason;
    // A called effect that is bounded (recursion): the non-local targets it may write. Empty with `mayWriteKnown` is not used.
    std::vector<const slang::ast::ValueSymbol*> mayWrite;
    bool mayWriteKnown = false;
    std::vector<const slang::ast::Statement*> scopes;
    uint64_t lo = 0, hi = 0;
    /// A statement before this one in its block (or in a subroutine it was expanded from) may have left it already: a `return`,
    /// `disable`, `break` or `continue`. The write certainly runs only when this is not set.
    bool afterPossibleExit = false;
};

// A write made by a system subroutine through an output argument or a mutating built-in method (queue, array, string).
// The extent it writes is not known, so it is kept apart from the assignments in `proceduralWrites`.
struct SystemWrite {
    const slang::ast::ProceduralBlockSymbol* process = nullptr;
    const slang::ast::InstanceSymbol* instance = nullptr;
    const slang::ast::ValueSymbol* target = nullptr;
    SourceLocation loc;
    bool forkOwnership = false;
    uint64_t lo = 0, hi = 0;
    std::string name;
};

// A read effect is owned by the calling process or continuous driver, never by a subroutine definition.
struct ProcessRead {
    const slang::ast::Symbol* process = nullptr;
    const slang::ast::InstanceSymbol* instance = nullptr;
    const slang::ast::ValueSymbol* target = nullptr;
    SourceLocation loc;
    std::string reason;
    bool forkOwnership = false;
    uint64_t lo = 0, hi = 0;
    bool exact = false;
    bool bounded = false;
};

struct ProcInfo {
    ProcKind kind = ProcKind::Other;
    SourceLocation loc;
    std::string instancePath;
};

enum class PairOperator : uint8_t { Equal, NotEqual, Xor, And };
struct PairOperatorFact {
    uint32_t proc = 0;
    const slang::ast::ValueSymbol* left = nullptr;
    const slang::ast::ValueSymbol* right = nullptr;
    PairOperator op = PairOperator::Xor;
    const slang::ast::Expression* rhs = nullptr; // complete continuous-assignment RHS for syntax-level proof
    const slang::ast::Symbol* anchor = nullptr;  // AST anchor for EvalContext when folding after model build
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

struct SyncResetGuard {
    NodeId node = kNoNode;
    bool activeLow = false;
    char forcedValue = '?';
    friend bool operator==(const SyncResetGuard&, const SyncResetGuard&) = default;
    friend bool operator<(const SyncResetGuard& a, const SyncResetGuard& b) {
        return std::tie(a.node, a.activeLow, a.forcedValue) < std::tie(b.node, b.activeLow, b.forcedValue);
    }
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
    bool hasIff = false;                                 // sensitivity event list used an `iff` qualifier (including constant `iff`)
    bool syncActiveLow = false;                          // polarity of the synchronous reset candidate (condition is a negation)
    NodeId syncReset = kNoNode;                          // the candidate's signal (control edges from it are ignored for chain recognition)
    std::vector<NodeId> syncResetGuards;                 // all proven constant forcing guards, per-bit
    std::vector<SyncResetGuard> syncResetFacts;          // all guards with exact polarity and forced bit value
    bool syncDataSuppressed = false;                     // a proven unconditional constant NBA overrides normal D
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
    bool simpleScalarConnection = false;
    bool conjunctiveConnection = false;                 // one identifier in the original instance connection
};

struct BlackBoxInst {
    std::string module;
    std::string hierarchy;
    SourceLocation loc;
    std::vector<BlackBoxPort> ports;
    bool declared = false;                               // a --library entry describes the ports (roles and directions known)
    bool attributed = false;                             // the module declares itself a black box; its body is not walked
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
    enum class Kind { Constant, Node, And, Or, Not, Equal, Unknown };
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
struct QualifierWrite {
    std::shared_ptr<const std::vector<GuardCondition>> guards;   // shared by every bit the assignment writes; never null
    std::shared_ptr<const std::vector<NodeId>> dataDependencies;
    std::shared_ptr<const std::vector<NodeId>> controlDependencies;
    bool dataApproximate = false;
    bool controlApproximate = false;
    bool uncertainCall = false;
};
struct CombQualifierArm {
    std::vector<GuardCondition> guards;
    std::shared_ptr<const std::vector<NodeId>> dataDependencies;
    std::shared_ptr<const std::vector<NodeId>> controlDependencies;
    bool dataApproximate = false;
    bool controlApproximate = false;
    bool uncertainCall = false;
    bool unassigned = false;
};
struct CombQualifierSummary {
    bool supported = false;
    bool multipleDrivers = false;
    bool incomplete = false;
    uint32_t proc = UINT32_MAX;
    std::vector<CombQualifierArm> arms;
};
struct BooleanDriver {
    BoolFactPtr value;
    bool unconditional = false;
};

struct CallUncertainty {
    std::string call, reason;
    SourceLocation location;
};

struct ConditionSite {
    SourceLocation loc;
    uint32_t proc;
    std::vector<NodeId> reads;
    std::string call;
    bool unresolved = false;
};

class ConeFinder;

class Model {
public:
    std::shared_ptr<ConeFinder> cones;                   // see sharedCones()
    /// Owned by rules/lint/value_range.cpp: the ranges of function formals, computed once per design and dropped with it.
    std::shared_ptr<void> formalRanges;
    /// Owned by rules/lint/value_range.cpp: proven bounds of accumulator registers, per design.
    std::shared_ptr<void> accumulatorRanges;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<ProcInfo> procs;
    std::vector<PairOperatorFact> pairOperatorFacts;
    std::vector<ProceduralWrite> proceduralWrites;
    std::vector<ProcessRead> processReads;
    std::vector<SystemWrite> systemWrites;
    // Copy-out effects at visible calls; values are not inferred from the callee body.
    std::vector<ProceduralWrite> copyBackWrites;
    /// The inventoried writes of one process in source order; the index is built on first use.
    const std::vector<const ProceduralWrite*>& writesOf(const slang::ast::ProceduralBlockSymbol* process) const;
    mutable std::unordered_map<const slang::ast::ProceduralBlockSymbol*, std::vector<const ProceduralWrite*>> writesByProcess_;
    mutable size_t writesIndexed_ = 0;
    /// Lazily built by `classifyReset`: the reset inventory (polarity bits per reset node and per reset symbol: 1 active high, 2 active
    /// low) and the nodes of every one-bit symbol.
    mutable bool resetIndexed_ = false;
    mutable std::unordered_map<NodeId, uint8_t> resetNodePolarity_;
    mutable std::unordered_map<const slang::ast::ValueSymbol*, uint8_t> resetSymPolarity_;
    mutable std::unordered_map<const slang::ast::ValueSymbol*, std::vector<NodeId>> oneBitNodes_;
    std::vector<ConditionSite> conditionSites; // assignment conditions, separate from compact graph edges
    std::vector<Flop> flops;
    std::vector<Domain> domains;
    std::vector<Fallback> fallbacks;
    std::vector<CallUncertainty> callUncertainties;
    std::unordered_map<NodeId, std::vector<uint32_t>> uncertainCallDrivers;
    std::unordered_map<NodeId, char> modeConstants;
    uint64_t casePrunedBranches = 0, casePrunedExpressions = 0;
    uint64_t subroutineSummaries = 0, subroutineBodyNodes = 0;
    std::vector<BlackBoxInst> blackBoxes;
    std::vector<std::string> subInstancePaths;           // elaborated sub-instance hierarchical paths (for get_cells resolution)
    std::vector<LibInstance> libInstances;               // instances of declared library cells (every kind)
    std::unordered_map<NodeId, std::vector<ConstDriver>> constDrivers;   // exact constant drivers per node (X/Z kept)
    std::vector<ClockGateInst> clockGates;
    std::unordered_set<const slang::ast::ValueSymbol*> loopVariables;   // procedural loop indices: no driver, and not design state
    std::unordered_set<const slang::ast::ValueSymbol*> procedureTemps;  // module-level variables every procedure writes before it reads (analysis/temps.h); also in loopVariables
    std::vector<RamInst> rams;

    // adjacency (edge indices), built by finish()
    Adjacency in, out;
    std::unordered_map<NodeId, uint32_t> flopOfQ;        // Q node -> index in flops
    std::unordered_map<NodeId, uint32_t> selfComb;       // node -> procedure of a combinational dependency on itself
    std::unordered_set<NodeId> selfCombProven;               // ... that Flow proves is a loop: the variable is read before any write and feeds its own write
    std::unordered_set<NodeId> selfCombApproximate;          // ... that exists only through an assignment with a computed index or a user call
    std::unordered_set<NodeId> combSelfRewritten;            // combinational nodes rewritten non-trivially from themselves in a procedure
    /// per register bit: for every data-carrying assignment, the (non-reset) control nodes guarding it; an empty list = unguarded.
    std::unordered_map<NodeId, std::vector<std::vector<NodeId>>> guards;
    std::unordered_map<NodeId, std::vector<QualifierWrite>> qualifierGuards;
    std::unordered_map<NodeId, CombQualifierSummary> combQualifierDrivers;
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
        if (flopIndexOfNode.size() == nodes.size() && q < flopIndexOfNode.size())
            return flopIndexOfNode[q] == UINT32_MAX ? nullptr : &flops[flopIndexOfNode[q]];
        auto it = flopOfQ.find(q);
        return it == flopOfQ.end() ? nullptr : &flops[it->second];
    }
    /// flopOfQ as a dense array, built once the model is complete (a hash lookup per visited node is costly on a SoC).
    std::vector<uint32_t> flopIndexOfNode;
    void indexFlops() {
        flopIndexOfNode.assign(nodes.size(), UINT32_MAX);
        for (const auto& [q, index] : flopOfQ) if (q < nodes.size()) flopIndexOfNode[q] = index;
    }
    /// Follows exact Comb (alias) edges backwards to the driving root.
    Root traceRoot(NodeId n) const;
    /// Follows single exact alias edges backwards as far as they go (the node whose value `n` is a plain copy of).
    NodeId aliasRoot(NodeId n) const;
    /// The register whose output `n` is a plain alias of, if any (`idx` receives its index in `flops`).
    const Flop* flopBehind(NodeId n, uint32_t* idx = nullptr) const;

    struct DeadEdgeKey {
        NodeId from = kNoNode, to = kNoNode;
        uint32_t proc = 0;
        uint8_t type = 0;
        bool operator==(const DeadEdgeKey&) const = default;
    };
    struct DeadEdgeHash {
        size_t operator()(const DeadEdgeKey& k) const noexcept {
            size_t h = std::hash<NodeId>{}(k.from);
            h ^= std::hash<NodeId>{}(k.to) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(k.proc) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<uint8_t>{}(k.type) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_set<DeadEdgeKey, DeadEdgeHash> deadEdges; // edges proved dead by Flow overwrite in the same procedure
    std::unordered_set<uint32_t> constantProcs;              // continuous procs whose RHS folds identically to a constant
    std::unordered_map<NodeId, std::vector<ConstDriver>> foldedConstDrivers; // continuous folded constant values (e.g. x ^ x -> '0')
    struct StuckFlopWrite {
        enum class Kind : uint8_t { Const, Copy, SelfXor, SelfOr, SelfAnd };
        Kind kind = Kind::Const;
        char constVal = '?';
        NodeId other = kNoNode;
        bool unconditional = false;
    };
    std::unordered_map<NodeId, std::vector<StuckFlopWrite>> stuckCandidates; // cleared in finish()
    std::unordered_set<NodeId> stuckRejected;                                // cleared in finish()
    std::unordered_map<NodeId, char> stuckInitValues;                        // cleared in finish()
    std::unordered_map<NodeId, char> stuckFlopValues;                        // single-bit flops proven stuck at '0' or '1'
    bool isDeadEdge(const Edge& e) const {
        return deadEdges.count({e.from, e.to, e.proc, static_cast<uint8_t>(e.type)}) != 0;
    }
    bool isConstantProc(uint32_t p) const { return constantProcs.count(p) != 0; }

    /// Exact, unconditional combinational literal behind aliases; '?' if not established (X/Z kept).
    char constantValue(NodeId n) const;

    void finish();   // builds adjacency, resolves clock roots, assigns domains
    void compactEdges();   // sorts edges and removes duplicates; building calls it rather than let the vector keep doubling
};

/// Returns the design's shared model, building it on first use.
Model& getModel(Design& design);

/// A top-level reset test of a procedure: the `if` can sit anywhere among the procedure's top-level statements, and a disjunction with a
/// reset operand (`rst || clear`) is one (its `then` runs whenever the reset is asserted; a conjunction is not). `reset` is the branch that
/// runs while the reset is asserted, `other` the one that runs otherwise (null without `else`). In a disjunction `reset` also runs
/// without the reset (`compound`): a write there is a reset write only while the tested net is at `assertedHigh`.
struct ResetBranch {
    const slang::ast::ConditionalStatement* test = nullptr;
    const slang::ast::Statement* reset = nullptr;
    const slang::ast::Statement* other = nullptr;
    const slang::ast::ValueSymbol* signal = nullptr;
    bool compound = false;
    bool assertedHigh = true;
};
/// The reset tests of one procedure by the identity of the tested net: `known` tests are resets, `undecided` ones may be (rules must not
/// make a definite claim from them). A test of data is in neither.
struct SyncReset {
    std::vector<ResetBranch> known;
    std::vector<ResetBranch> undecided;
};

/// Reset identity of a one-bit net, three outcomes. KNOWN (`reset`): the reset inventory (a net that is an asynchronous reset in an edge
/// list, or a plain alias of one through ports and assignments, judged per instance node; the polarity is the edge's, never the name's)
/// or a name that is a reset name with no doubt. DATA (neither flag): a reset word followed by a status or request word, or no reset word at
/// all. UNDECIDED (`undecided`): anything else, and a net whose instances disagree. A net that is an asynchronous reset of both polarities
/// is a reset whose polarity is unknown (`polarityKnown` false) unless its name says which; only a procedure's own edge on it decides.
struct ResetClass {
    bool reset = false;
    bool undecided = false;
    bool fromInventory = false;
    bool activeLow = false;
    bool polarityKnown = true;
};
/// The one reset classifier: LNT-020/054/067/068 and the RDC reset-likeness use it.
ResetClass classifyReset(const Model&, const slang::ast::ValueSymbol&);
ResetClass classifyResetNode(const Model&, NodeId);

/// Name evidence alone. `Known`: some token is or ends in `rst`/`reset` (`rst`, `nrst`, `sysrst`, `presetn`, `wrst`) or is `por`, and only
/// polarity, sync or domain tokens (`n`, `ni`, `b`, `i`, `sync`, `core`, `aon`, `lc`, ...) or another reset token follow it (`por_rst_n`).
/// `Data`: no such token (`first`, `burst`, `restore`, `presets`), or no clean reset token and a status word after one (`rst_done`,
/// `reset_pending`). `Undecided`: a reset token that is not clean otherwise (`rst_q`, `rst_gen_n`, `rst_start`) or a clean one with a
/// status or request qualifier before it (`force_rst`, `en_rst_n`, `do_reset`).
struct ResetName {
    enum class Verdict { Data, Known, Undecided } verdict = Verdict::Data;
    bool resetWord = false;   // a reset token was found (Data with this set is a status/request name)
    bool activeLow = false;
};
ResetName classifyResetName(std::string_view name);
/// 0 not a known reset name, 1 a known reset name, 2 a known reset name that reads active low.
int resetNameKind(std::string_view name);
inline bool resetName(std::string_view name) { return resetNameKind(name) != 0; }

/// Direct top-level reset tests; reset identity follows `classifyReset`. Null when the procedure tests no reset-like net.
std::optional<SyncReset> synchronousReset(const slang::ast::ProceduralBlockSymbol&, const Model&,
                                          std::vector<const slang::ast::ConditionalStatement*>* enables = nullptr);
/// `enables`, when given, receives the tests of a net whose identity is undecided that guard an arm which only loads data
/// (`if (valid) q <= d;`): enables, in neither list and no reset question.

/// Whether `write` stores a constant while the net `branch.signal` is held asserted, whatever else it reads (`q <= rst ? 1'b0 : din`
/// under `rst || load`). Only a plain assignment can; anything not evaluated to a constant is not.
bool storesConstantWhileAsserted(const ProceduralWrite& write, const ResetBranch& branch);

/// The truth of a condition inside the branch of a disjunctive reset test while the tested net `branch.signal` is held asserted: null when
/// it does not evaluate to a constant that way (it reads anything else).
std::optional<bool> conditionWhileAsserted(const slang::ast::Expression& condition, const slang::ast::ProceduralBlockSymbol& process,
                                           const ResetBranch& branch);

/// Whether `write`, which sits in the branch of a disjunctive reset test, runs while the tested net is asserted: `Runs` when every
/// condition between the branch and the write is constant that way and selects the write's arm (`if (rst) q <= 0;` inside `rst || load`),
/// `Skipped` when one selects the other arm (`else q <= din;` there), else `Unknown`.
enum class Reach : uint8_t { Runs, Skipped, Unknown };
Reach reachWhileAsserted(const ProceduralWrite& write, const ResetBranch& branch);

/// Heap bytes per structure of the model (sizes times element sizes, by capacity), for --timings. Names are not stored: a node's name
/// is built from its symbol on demand.
std::vector<std::pair<std::string, uint64_t>> modelMemory(const Model&);

/// Builds the model from an elaborated design. Never throws for unsupported constructs; they become
/// `fallbacks`.
std::unique_ptr<Model> buildModel(Design& design);

} // namespace sverity::model
