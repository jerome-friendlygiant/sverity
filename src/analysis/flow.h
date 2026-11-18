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
//   * a loop that is not unrolled (`for` with a run-time bound, `while`, a `foreach` with dynamic dimensions or a jump, ...) is walked once: a value written late in a loop body and
//     read early in the next iteration is not seen (`inLoop`, `walkedOnce`),
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
class SubroutineSymbol;
class Symbol;
class ValueSymbol;
class Expression;
class CallExpression;
class AssignmentExpression;
class UnaryExpression;
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
    bool inWalkedLoop = false;    // inside a loop that is walked once, not followed through its iterations
    const slang::ast::AssignmentExpression* asg = nullptr;   // the assignment (write) or innermost assignment holding the read
    const slang::ast::UnaryExpression* step = nullptr;       // the ++ / -- that made this read and write of its operand (both carry it)
    slang::SourceLocation loc;
    const GuardList* guards = nullptr;   // never null after analysis; the procedure root has an empty list

    // ---- results, reads
    bool entryMaybe = false;      // the value from before the procedure can be the one read, on at least one path
    bool entryOnly = false;       // ... on every path
    bool entryHard = false;       // some bit of the read can still hold the entry value although no write that could fill it is there: no run-time-indexed write and no write in a walked-once loop reaches that bit on the way
    /// The bits of a read that can still hold the entry value, with how sure that is: Sole (the entry value is the only one that
    /// reaches them), Some (it reaches them on some paths, the other writes being exact), Unsure (a run-time-indexed write or a write in a
    /// loop that is not followed may have filled them).
    enum class Entry : uint8_t { Sole, Some, Unsure };
    struct EntrySeg { Range range; Entry kind; };
    std::vector<EntrySeg> entrySegs;
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
    /// Analyses a subroutine body: the entry value of a local is its value before any assignment.
    explicit Flow(const slang::ast::SubroutineSymbol& routine);
    ~Flow();
    Flow(const Flow&) = delete;
    Flow& operator=(const Flow&) = delete;

    std::vector<Access> accesses;     // in evaluation order (right-hand side before the target of an assignment)
    std::vector<Kill> kills;
    bool opaque = false;              // a user subroutine call or a mid-body timing control was seen (see Access::exposed / used)
    bool timing = false;              // ... specifically a mid-body timing control: other processes can run inside this activation
    bool bad = false;                 // slang could not analyse (invalid statement); results are incomplete
    bool approximated = false;        // a `continue`, or a case whose items cannot all match, makes the path analysis approximate: absence of a latch or of a write is not proven
    bool truncated = false;           // a `for` loop was not unrolled (also sets `approximated`): over the procedure's budget, a bound that is not constant, a counter the body writes, an unsupported shape. It was walked once with its counter unknown, so what is unassigned at the end may be assigned there
    bool overBudget = false;          // a loop beyond the unrolling budget was walked once: which accesses it covers is not recorded (`touchedInWalkedLoop` cannot tell)
    bool pathsDropped = false;        // a `continue` or a case that cannot match dropped paths of the walk (also sets `approximated`): what is absent from the end state is not proven absent
    bool walkedOnce = false;          // some loop was walked once, not followed through its iterations (a `for` loop that is not unrolled, `while`, `do`, `repeat`, `forever`, a `foreach` that is not followed): a read, or what is unassigned, may depend on an iteration the walk did not see

    /// True when some access to `sym` (a read or a write) lies inside a loop that was walked once: only then can that approximation
    /// change what the walk concludes about `sym`. Always true after `overBudget`.
    bool touchedInWalkedLoop(const slang::ast::ValueSymbol& sym) const {
        if (overBudget) return true;
        for (const auto& a : accesses)
            if (a.sym == &sym && a.inWalkedLoop) return true;
        return false;
    }
    /// Bit ranges of `sym` whose final value can still be the entry value (not written on every path). Empty = definitely assigned.
    std::vector<Range> unassignedAtEnd(const slang::ast::ValueSymbol& sym) const;
    /// Bit ranges of `sym` that a read made by the user call `call` (its body, not its arguments) can still see at their value from
    /// before the procedure. The whole symbol when the call was not reached or `sym` was not written before it.
    std::vector<Range> entryRangesAtCall(const slang::ast::CallExpression& call, const slang::ast::ValueSymbol& sym) const;
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
