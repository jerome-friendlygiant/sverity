// Flow-sensitive statement analysis of one procedure (always*, initial/final bodies, continuous assignments).
//
// One walk over the statement tree records every read and write of a variable in evaluation order together with:
//   * the bit range touched (exact when the select is static, the longest static prefix otherwise),
//   * the path guards under which it runs (guards.h),
//   * the reaching definitions: which earlier writes can supply the value a read sees, and whether the value from before the
//     procedure ("entry") can still be seen on some / on every path.
// Rules ask questions of the result (read before write, dead store, overridden write, definite assignment at the end of the
// procedure) instead of re-implementing their own statement walks.
//
// Built on slang's AbstractFlowAnalysis (MIT): the base class supplies the control-flow skeleton (if / case incl. statically known
// branches and full-coverage cases, loops incl. unrolled constant for-loops, break/continue/return/disable, fork-join). This file
// supplies the lattice (reaching-definition sets per bit interval). The per-interval representation follows the idea used by
// slang's DataFlowAnalysis and by slang-netlist's ValueTracker (interval maps rather than per-bit arrays, union at joins; MIT);
// nothing is copied. Documented limits (a rule must treat them as "unknown", never "safe"):
//   * loops are walked once: a value written late in a loop body and read early in the next iteration is not seen (`inLoop`),
//   * a call to a user subroutine and any timing control inside the body are opaque (`opaque`): every live write counts as used,
//   * blocking writes update what later reads see; nonblocking writes only the final value (reads still see the old value).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "analysis/guards.h"
#include "slang/text/SourceLocation.h"

namespace slang::ast {
class ProceduralBlockSymbol;
class Symbol;
class ValueSymbol;
class Expression;
class AssignmentExpression;
class Statement;
class EvalContext;
} // namespace slang::ast

namespace sverity::flow {

using Range = std::pair<uint64_t, uint64_t>;   // inclusive bit range within the symbol (flattened, LSB = 0)

/// How a procedure is triggered: always_comb / always @* (Comb), always_latch (Latch), edge-controlled or always_ff (Seq),
/// always @(a or b) without edges (Level), anything else (Other: initial, final, mixed or non-signal events).
enum class Style { Comb, Latch, Seq, Level, Other };
Style styleOf(const slang::ast::ProceduralBlockSymbol& p);
/// The procedure body without its outer event control.
const slang::ast::Statement& bodyOf(const slang::ast::ProceduralBlockSymbol& p);

struct Access {
    const slang::ast::ValueSymbol* sym = nullptr;
    Range range{0, 0};
    bool write = false;
    bool exact = true;            // the range is the exact set of bits touched (static select); false = a dynamic select within `range`
    bool whole = false;           // the access covers the whole symbol exactly
    bool nonblocking = false;     // write: nonblocking assignment
    bool compound = false;        // write: compound assignment (the target is also read, as a separate Read access)
    bool local = false;           // the symbol is declared inside the procedure (not module state)
    bool inLoop = false;          // under a loop body that is walked once (not unrolled)
    const slang::ast::AssignmentExpression* asg = nullptr;   // the assignment (write) or innermost assignment holding the read
    slang::SourceLocation loc;
    const GuardList* guards = nullptr;   // never null after analysis; the procedure root has an empty list

    // ---- results, reads
    bool entryMaybe = false;      // the value from before the procedure can be the one read, on at least one path
    bool entryOnly = false;       // ... on every path
    std::vector<uint32_t> reaching;   // indices (into Flow::accesses) of the writes that can supply the value

    // ---- results, writes
    bool used = false;            // some read can see this write
    bool observed = false;        // a timing control after this write lets other processes see it
    bool exposed = false;         // a user subroutine call after this write may have read it (unknown, not a proven use)
    bool live = false;            // the write is part of the final value at the end of the procedure
};

/// `victim` is replaced by `killer` (a whole-variable write) on at least one path. Rules decide from the flags whether this
/// is a defect: it never is when the victim was read (`readBefore`) or observed by another process in between.
struct Kill {
    uint32_t victim = 0, killer = 0;       // indices into Flow::accesses
    bool readBefore = false;               // some read before the killer can see the victim
    bool observed = false;                 // a timing control between them lets another process see the victim
    bool exposed = false;                  // a user subroutine call between them may have read the victim (unknown)
};

class Flow {
public:
    /// Analyses `proc` (any always*, initial or final procedure).
    explicit Flow(const slang::ast::ProceduralBlockSymbol& proc);
    ~Flow();
    Flow(const Flow&) = delete;
    Flow& operator=(const Flow&) = delete;

    const slang::ast::ProceduralBlockSymbol& proc;
    std::vector<Access> accesses;     // in evaluation order (right-hand side before the target of an assignment)
    std::vector<Kill> kills;
    bool opaque = false;              // a user subroutine call or a mid-body timing control was seen (see Access::exposed / used)
    bool bad = false;                 // slang could not analyse (invalid statement); results are incomplete
    bool approximated = false;        // a `continue`, or a case whose items cannot all match, makes the path analysis approximate: absence of a latch or of a write is not proven

    /// Bit ranges of `sym` whose final value can still be the entry value (not written on every path). Empty = definitely assigned.
    std::vector<Range> unassignedAtEnd(const slang::ast::ValueSymbol& sym) const;
    /// Every symbol written anywhere in the procedure, in first-write order.
    std::vector<const slang::ast::ValueSymbol*> writtenSymbols() const;
    /// True when the two accesses can never execute in the same activation (structurally exclusive guards).
    /// A comparison-based exclusivity (x==1 versus x==2) is withdrawn when its operand is written between the two accesses.
    bool exclusive(const Access& a, const Access& b) const;
    uint32_t indexOf(const Access& a) const { return static_cast<uint32_t>(&a - accesses.data()); }
    /// True when `inner`'s path condition implies `outer`'s (inner executes only if outer's guards held).
    static bool guardImplies(const Access& inner, const Access& outer) { return GuardMap::implies(*inner.guards, *outer.guards); }
    /// True when some write to `sym` lies strictly between accesses `from` and `to` in evaluation order.
    bool writtenBetween(const slang::ast::ValueSymbol& sym, uint32_t from, uint32_t to) const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace sverity::flow
