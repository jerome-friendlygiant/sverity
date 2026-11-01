// Path guards: the conditions under which a statement of a procedure executes.
//
// A guard list is a conjunction of literals (if-branch taken or not, case arm selected, loop body entered). It is computed from the
// statement tree of one procedure, independently of any dataflow, so the flow analysis (flow.h) and the model builder can attach the
// same path condition to every read and write. `exclusive()` answers "can these two guard lists never hold together?" structurally
// (different branches of one if/case) or with a small constant-comparison form (x==1 versus x==2, x versus !x). Everything else is
// "not provably exclusive": callers treat it as possibly simultaneous.
//
// Provenance: the idea of carrying a per-path condition comes from slang-netlist's conditional nodes (MIT, J. Hanlon), but there a
// single "latest condition" pointer is kept; here the full conjunction is kept so AND and OR structure is not lost. The
// constant-comparison exclusivity test is the former CondForm of lint_flow.cpp.
#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace slang::ast {
class EvalContext;
class Expression;
class Statement;
} // namespace slang::ast

namespace sverity::flow {

struct Guard {
    enum class Kind : uint8_t {
        If,           // branch of an if/?: statement; `taken` says which
        CaseArm,      // arm `arm` of a case statement (matched, and no earlier arm matched)
        CaseDefault,  // no arm of the case matched
        Loop,         // inside a loop body (zero or more executions)
    };
    Kind kind = Kind::If;
    bool taken = true;                                    // If: true branch (or the loop condition for Loop)
    const slang::ast::Expression* cond = nullptr;         // If: the single condition; null when the if has several (`&&&`)
    const slang::ast::Statement* owner = nullptr;         // the if/case/loop statement
    uint32_t arm = 0;                                     // CaseArm index
};

using GuardList = std::vector<Guard>;

/// An identifier or select chain with no calls, so two occurrences in one expression denote one value.
bool isPureRef(const slang::ast::Expression& e);

/// True when `a` (holding if `ta`, else its negation) and `b` (likewise `tb`) can never both hold: comparisons of one runtime operand
/// with different constants, or an operand against its own negation. Valid only while that operand keeps its value.
bool conditionsExclusive(slang::ast::EvalContext& ec, const slang::ast::Expression& a, bool ta, const slang::ast::Expression& b, bool tb);

class GuardMap {
public:
    /// Builds the guard list of every statement under `root` (the procedure body).
    GuardMap(const slang::ast::Statement& root, slang::ast::EvalContext& ec);
    /// Guards enclosing `s` (the conditions of s itself are not included); null for a statement not under `root`.
    const GuardList* of(const slang::ast::Statement* s) const {
        auto it = map_.find(s);
        return it == map_.end() ? nullptr : &it->second;
    }
    /// True when no assignment under both lists can execute in the same activation. When the proof rests on comparing runtime
    /// operands with constants (x==1 versus x==2), those operand expressions are appended to `operands`: the proof only holds
    /// while they keep their value, which the caller must check.
    bool exclusive(const GuardList& a, const GuardList& b, std::vector<const slang::ast::Expression*>* operands = nullptr) const;
    /// True when `inner` implies `outer` (every literal of `outer` also appears in `inner`): `inner` runs only when `outer` ran.
    static bool implies(const GuardList& inner, const GuardList& outer);
    bool inLoop(const GuardList& g) const;

private:
    void walk(const slang::ast::Statement& s, GuardList cur);
    bool condsExclusive(const slang::ast::Expression& a, bool ta, const slang::ast::Expression& b, bool tb,
                        std::vector<const slang::ast::Expression*>* operands) const;
    slang::ast::EvalContext& ec_;
    std::unordered_map<const slang::ast::Statement*, GuardList> map_;
};

} // namespace sverity::flow
