// Which variables are scratch space of a procedure and which are state.
//
// A read is "dominated" when every path from the start of the procedure activation to it has already written the whole variable
// in the same activation (typically the `for` initialiser of a counter). A variable whose reads are all dominated, in every
// procedure and subroutine that uses it, and that nothing else reads (no continuous assignment, port connection, hierarchical
// reference or event control), never carries a value from one activation to another or between processes: it is a procedure
// temporary. It is not state, so other processes writing it are no multiple drivers, and a dependency through it is no
// combinational loop. The walk is structural and does not depend on Flow, so it holds where Flow is approximate.
#pragma once
#include <set>
#include <unordered_set>

namespace slang::ast {
class Compilation;
class Statement;
class ValueSymbol;
} // namespace slang::ast

namespace sverity::analysis {

/// What one procedure or subroutine body does to its variables (nets and variables; constants are ignored).
struct BodyReads {
    std::set<const slang::ast::ValueSymbol*> read;      // read somewhere in the body
    std::set<const slang::ast::ValueSymbol*> exposed;   // ... of which some read is not dominated by a whole write, or is an event control, a hierarchical reference or a construct not followed
    std::set<const slang::ast::ValueSymbol*> written;   // written somewhere in the body
    std::set<const slang::ast::ValueSymbol*> nonblocking;   // ... by a nonblocking assignment
    /// True when every read of `sym` is dominated by a whole write in the same activation.
    bool dominated(const slang::ast::ValueSymbol* sym) const { return read.count(sym) && !exposed.count(sym); }
};

/// The reads of one body. A write is a definition only when it is a blocking assignment of the whole variable; a nonblocking or
/// partial write is not. Unstructured flow (a timing control, `fork`, a pattern `case`, an assertion) clears the definitions or
/// exposes the variables it names.
BodyReads readsOf(const slang::ast::Statement& body);

/// The module-level variables of the elaborated design that are procedure temporaries (see above). Ports, interface members,
/// package variables and variables that are only written are never temporaries.
std::unordered_set<const slang::ast::ValueSymbol*> procedureTemporaries(slang::ast::Compilation& compilation);

} // namespace sverity::analysis
