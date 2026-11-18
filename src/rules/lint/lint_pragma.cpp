// LNT-083: synthesis pragmas written as comments (full_case, parallel_case, translate_off / translate_on).
#include <algorithm>
#include <tuple>
#include <map>
#include <set>

#include "rules/rules.h"
#include "sverity/lint.h"
#include "synth_pragma.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/NetType.h"
#include "slang/numeric/SVInt.h"
#include "slang/syntax/AllSyntax.h"

namespace sverity {
namespace {

using namespace slang;
using namespace slang::ast;
using namespace lint;
namespace syn = slang::syntax;

// The values a plain `case` leaves without an item and the ones two items share, when every item is a constant: what the pragma
// would change. Nothing for a wildcard case, a selector wider than 16 bits or an item that is not a constant.
struct CaseFacts {
    bool decided = false;
    std::vector<uint64_t> uncovered, shared;
    uint64_t uncoveredCount = 0;
};

CaseFacts factsOf(Lint& l, const CaseStatement& c) {
    CaseFacts f;
    const uint32_t w = c.expr.type->getBitWidth();
    if (c.condition != CaseStatementCondition::Normal || !c.expr.type->isIntegral() || w == 0 || w > 16) return f;
    std::map<uint64_t, int> seen;
    for (size_t g = 0; g < c.items.size(); g++)
        for (const auto* e : c.items[g].expressions) {
            const auto v = l.evalConst(*e);
            if (!v.isInteger() || v.integer().hasUnknown()) return f;
            const auto value = v.integer().as<uint64_t>();
            if (!value) return f;
            // two expressions of one item name one value twice, which is no overlap between items
            auto& n = seen[*value];
            if (n != 0 && n != static_cast<int>(g) + 1) f.shared.push_back(*value);
            n = static_cast<int>(g) + 1;
        }
    f.decided = true;
    for (uint64_t v = 0; v < (uint64_t(1) << w); v++)
        if (!seen.count(v) && !c.defaultCase) {
            f.uncoveredCount++;
            if (f.uncovered.size() < 3) f.uncovered.push_back(v);
        }
    return f;
}

std::string listOf(const std::vector<uint64_t>& v) {
    std::string s;
    for (auto x : v) s += (s.empty() ? "" : ", ") + std::to_string(x);
    return s;
}

// What writes design state, found one node at a time so that a caller can walk a tree itself. A written symbol that is module-level
// (a signal of this module, of a child it names hierarchically, or of a package) is recorded; a call into a user subroutine adds the
// state the subroutine writes, transitively. `unknown` is set when a callee cannot be read (a DPI import, a class method).
struct WriteSet {
    std::set<const Symbol*> written;
    bool unknown = false;
    std::set<const SubroutineSymbol*> seen;
};

void noteLvalue(const Expression& target, WriteSet& out) {
    const auto symbols = lvalueSymbols(target);
    if (symbols.empty()) out.unknown = true;   // a target that names no symbol is not understood: the region is not self-contained
    for (const auto* sym : symbols)
        if (isModuleLevel(*sym)) out.written.insert(sym);
}

void nodeWrites(const Expression& e, WriteSet& out);

struct CalleeWrites : ASTVisitor<CalleeWrites, VisitFlags::AllGood> {
    explicit CalleeWrites(WriteSet& w) : out(w) {}
    WriteSet& out;
    template <typename T>
    void handle(const T& t) {
        if constexpr (std::is_base_of_v<Expression, T>) nodeWrites(t, out);
        if constexpr (std::is_same_v<T, ProceduralDeassignStatement>) noteLvalue(t.lvalue, out);
        visitDefault(t);
    }
};

void nodeWrites(const Expression& e, WriteSet& out) {
    switch (e.kind) {
        case ExpressionKind::Assignment: noteLvalue(e.as<AssignmentExpression>().left(), out); return;
        case ExpressionKind::UnaryOp: {
            const auto& u = e.as<UnaryExpression>();
            if (u.op == UnaryOperator::Preincrement || u.op == UnaryOperator::Postincrement || u.op == UnaryOperator::Predecrement ||
                u.op == UnaryOperator::Postdecrement)
                noteLvalue(u.operand(), out);
            return;
        }
        case ExpressionKind::Call: {
            const auto& c = e.as<CallExpression>();
            if (c.isSystemCall()) return;
            const auto* sub = std::get<const SubroutineSymbol*>(c.subroutine);
            if (c.thisClass() || sub->flags.has(MethodFlags::DPIImport) || sub->flags.has(MethodFlags::Virtual) ||
                (sub->getParentScope() && sub->getParentScope()->asSymbol().kind == SymbolKind::ClassType)) {
                out.unknown = true;
                return;
            }
            if (out.seen.insert(sub).second) {
                CalleeWrites body(out);
                sub->getBody().visit(body);
            }
            // a `ref` actual is written whenever the callee writes its formal; every non-const one counts, as the callee may
            const auto formals = sub->getArguments();
            for (size_t i = 0; i < formals.size() && i < c.arguments().size(); ++i) {
                if (formals[i]->direction != ArgumentDirection::Ref || formals[i]->flags.has(VariableFlags::Const)) continue;
                const Expression* actual = c.arguments()[i];
                if (actual->kind == ExpressionKind::Assignment) actual = &actual->as<AssignmentExpression>().left();
                noteLvalue(*actual, out);
            }
            return;
        }
        default: return;
    }
}

class SynthesisPragmaRule final : public LintRule {
public:
    SynthesisPragmaRule() : LintRule("LNT-083") {}

private:
    // What a region switched off for synthesis holds. A construct wholly inside it counts as such; a region that starts or ends
    // inside a procedure, a case, an assignment or an instance counts the statements, case items and expression parts it covers.
    struct Inside {
        size_t continuous = 0, always = 0, initial = 0, declared = 0, statements = 0, caseItems = 0, parts = 0, instances = 0;
        size_t simOnly = 0, unknown = 0;   // statements proven simulation-only; content that could not be classified
        size_t cuts = 0;                   // innermost expressions the region boundary cuts (see expressionIn)
        std::set<std::tuple<int, uint32_t, size_t>> seen;
    };
    enum Category { Cont, Proc, Decl, Stmt, Item, Part, Inst, Sim, Unk };

    struct Scan : HardwareVisitor<Scan, VisitFlags::AllCanonical> {
        Scan(Lint& lint, const PragmaIndex& idx) : l(lint), pragmas(idx) {}
        Lint& l;
        const PragmaIndex& pragmas;
        std::set<std::pair<uint32_t, size_t>> casesDone;
        std::map<size_t, Inside> inside;   // by region
        // For the question whether a region is a self-contained simulation checker: what it declares, what it writes of the design's
        // state, and every place a design signal is named.
        std::map<size_t, std::set<const Symbol*>> declaredIn, writtenIn;
        std::set<size_t> uncertainIn;      // regions that call something whose writes cannot be read
        std::vector<std::pair<const Symbol*, slang::SourceLocation>> references;
        std::optional<size_t> wholeIn;     // the region holding the whole procedure or assignment being visited
        int simDepth = 0;                  // inside a statement proven simulation-only
        int procDepth = 0;                 // inside a procedure

        struct Span { uint32_t buffer; size_t lo, hi; };
        std::optional<Span> spanOf(slang::SourceRange r) const {
            if (!r.start().valid() || !r.end().valid()) return std::nullopt;
            const auto s = l.sm().getFullyExpandedLoc(r.start()), e = l.sm().getFullyExpandedLoc(r.end());
            if (s.buffer().getId() != e.buffer().getId()) return std::nullopt;
            return Span{s.buffer().getId(), s.offset(), std::max(e.offset(), s.offset())};
        }
        enum class Rel { None, Inside, Cuts };
        Rel relate(const Span& s, size_t region) const {
            const auto& r = pragmas.regions[region];
            if (r.begin.buffer().getId() != s.buffer || s.hi <= r.begin.offset() || s.lo >= r.end) return Rel::None;
            return s.lo >= r.begin.offset() && s.hi <= r.end ? Rel::Inside : Rel::Cuts;
        }
        std::optional<size_t> regionOf(slang::SourceLocation at) const {
            if (!at.valid()) return std::nullopt;
            at = l.sm().getFullyExpandedLoc(at);
            for (size_t i = 0; i < pragmas.regions.size(); i++) {
                const auto& r = pragmas.regions[i];
                if (r.begin.buffer().getId() == at.buffer().getId() && at.offset() >= r.begin.offset() && at.offset() < r.end) return i;
            }
            return std::nullopt;
        }
        // Counts a construct once per source position, whatever number of instances elaborate it.
        bool once(size_t region, Category what, slang::SourceLocation at) {
            const auto exp = l.sm().getFullyExpandedLoc(at);
            return inside[region].seen.insert({what, exp.buffer().getId(), exp.offset()}).second;
        }
        Inside* first(slang::SourceLocation at, Category what) {
            const auto region = regionOf(at);
            if (!region || !l.isUser(at)) return nullptr;
            return once(*region, what, at) ? &inside[*region] : nullptr;
        }
        void handle(const ContinuousAssignSymbol& s) {
            const auto region = regionOf(s.location);
            if (auto* in = first(s.location, Cont)) in->continuous++;
            auto saved = wholeIn;
            wholeIn = region;
            visitDefault(s);
            wholeIn = saved;
        }
        void handle(const ProceduralBlockSymbol& p) {
            const auto region = regionOf(p.location);
            bool simulationOnly = false;
            if (auto* in = first(p.location, Proc)) {
                if (p.procedureKind == ProceduralBlockKind::Initial) {
                    if (writesState(p.getBody())) in->initial++;
                    else { in->simOnly++; simulationOnly = true; }
                }
                else if (p.procedureKind != ProceduralBlockKind::Final) in->always++;
                else { in->simOnly++; simulationOnly = true; }
            }
            auto saved = wholeIn;
            wholeIn = region;
            simDepth += simulationOnly;
            procDepth++;
            visitDefault(p);
            procDepth--;
            simDepth -= simulationOnly;
            wholeIn = saved;
        }
        static bool writesState(const Statement& body) {
            WriteSet w;
            CalleeWrites v(w);
            body.visit(v);
            return !w.written.empty() || w.unknown;
        }
        // A port is the module's interface, whatever the region says about its declaration: writing it is driving the design's output.
        static bool isPort(const Symbol& sym) {
            const auto* body = enclosingBody(sym);
            return body && body->findPort(sym.name) != nullptr;
        }
        void handle(const VariableSymbol& v) {
            if (isModuleLevel(v) && v.kind == SymbolKind::Variable) {
                if (auto* in = first(v.location, Decl)) in->declared++;
                if (const auto region = regionOf(v.location); region && l.isUser(v.location) && !isPort(v)) declaredIn[*region].insert(&v);
            }
        }
        void handle(const NetSymbol& n) {
            if (isModuleLevel(n)) {
                if (auto* in = first(n.location, Decl)) in->declared++;
                if (const auto region = regionOf(n.location); region && l.isUser(n.location) && !isPort(n)) declaredIn[*region].insert(&n);
            }
        }
        // An instance inside a region: synthesis does not build it.
        void handle(const InstanceSymbol& s) {
            const auto* parent = s.getParentScope();
            if (parent && parent->asSymbol().kind != SymbolKind::CompilationUnit && !s.isTopLevel()) {
                const auto region = regionOf(s.location);
                if (region && wholeIn != region)
                    if (auto* in = first(s.location, Inst)) {
                        bool drives = false;
                        for (auto* pc : s.getPortConnections())
                            if (pc && pc->port.kind == SymbolKind::Port && pc->port.as<PortSymbol>().direction != ArgumentDirection::In && pc->getExpression())
                                drives = true;
                        (drives ? in->instances : in->unknown)++;
                    }
            }
            visitDefault(s);
        }
        // A statement wholly inside a region that only prints, asserts or ends the simulation changes nothing synthesis builds.
        void handle(const ExpressionStatement& s) {
            bool sim = false;
            if (s.expr.kind == ExpressionKind::Call) {
                sim = s.expr.as<CallExpression>().isSystemCall();
                if (!pragmas.regions.empty())
                    if (const auto span = spanOf(s.sourceRange))
                        for (size_t i = 0; i < pragmas.regions.size(); i++)
                            if (wholeIn != i && relate(*span, i) == Rel::Inside && l.isUser(s.sourceRange.start()) && once(i, sim ? Sim : Unk, s.sourceRange.start()))
                                (sim ? inside[i].simOnly : inside[i].unknown)++;
            }
            simDepth += sim;
            visitDefault(s);
            simDepth -= sim;
        }
        void handle(const ImmediateAssertionStatement& s) {
            simulationOnly(s, s.sourceRange);
        }
        void handle(const ConcurrentAssertionStatement& s) {
            simulationOnly(s, s.sourceRange);
        }
        // The design state a node of a region writes, whatever procedure or statement holds it and whether or not that one is
        // simulation-only: an increment, a compound assignment, a force, a write made by a called task or function, an output port.
        void recordWrites(const Expression& e, slang::SourceRange range) {
            WriteSet w;
            nodeWrites(e, w);
            noteRegionWrites(w, range);
        }
        void noteRegionWrites(const WriteSet& w, slang::SourceRange range) {
            if (w.written.empty() && !w.unknown) return;
            const auto span = spanOf(range);
            for (size_t i = 0; i < pragmas.regions.size(); i++) {
                if (!span) { uncertainIn.insert(i); continue; }
                if (relate(*span, i) != Rel::Inside) continue;
                writtenIn[i].insert(w.written.begin(), w.written.end());
                if (w.unknown) uncertainIn.insert(i);
            }
        }
        void handle(const ProceduralDeassignStatement& s) {
            WriteSet w;
            noteLvalue(s.lvalue, w);
            if (!pragmas.regions.empty() && l.isUser(s.sourceRange.start())) noteRegionWrites(w, s.sourceRange);
            visitDefault(s);
        }
        template <typename T>
        void simulationOnly(const T& s, slang::SourceRange range) {
            if (const auto span = spanOf(range))
                for (size_t i = 0; i < pragmas.regions.size(); i++)
                    if (wholeIn != i && relate(*span, i) == Rel::Inside && l.isUser(range.start()) && once(i, Sim, range.start())) inside[i].simOnly++;
            simDepth++;
            visitDefault(s);
            simDepth--;
        }
        // Expressions: an assignment wholly inside a region writes state there; the innermost expression a region boundary cuts
        // through has a part of it switched off, which changes the value synthesis builds.
        template <typename T>
        void handle(const T& t) {
            if constexpr (std::is_base_of_v<Expression, T>) {
                if constexpr (std::is_same_v<T, NamedValueExpression>)
                    if (!pragmas.regions.empty() && l.isUser(t.sourceRange.start())) references.push_back({&t.symbol, t.sourceRange.start()});
                if (!pragmas.regions.empty() && l.isUser(t.sourceRange.start())) recordWrites(t, t.sourceRange);
                if (pragmas.regions.empty() || simDepth > 0 || !l.isUser(t.sourceRange.start())) {
                    visitDefault(t);
                    return;
                }
                const auto span = spanOf(t.sourceRange);
                std::map<size_t, size_t> cutsBefore;
                for (const auto& [region, in] : inside) cutsBefore[region] = in.cuts;
                visitDefault(t);
                if (!span) return;
                for (size_t i = 0; i < pragmas.regions.size(); i++) {
                    if (wholeIn == i) continue;
                    const auto rel = relate(*span, i);
                    if (rel == Rel::Inside) {
                        if constexpr (std::is_same_v<T, AssignmentExpression>) {
                            bool state = false;
                            for (const auto* sym : lvalueSymbols(t.left())) state = state || isModuleLevel(*sym);
                            if (state && procDepth > 0 && once(i, Stmt, t.sourceRange.start())) inside[i].statements++;
                        }
                    }
                    else if (rel == Rel::Cuts) {
                        const auto it = inside.find(i);
                        const size_t was = cutsBefore.count(i) ? cutsBefore[i] : 0;
                        if ((it == inside.end() ? 0 : it->second.cuts) == was) {
                            if (once(i, Part, t.sourceRange.start())) inside[i].parts++;
                            inside[i].cuts++;
                        }
                    }
                }
            }
            else {
                // Declarations a region holds that are neither state nor simulation code: not classified.
                if constexpr (std::is_base_of_v<Symbol, T>) {
                    switch (t.kind) {
                        case SymbolKind::Subroutine: case SymbolKind::Parameter: case SymbolKind::TypeParameter: case SymbolKind::TypeAlias:
                        case SymbolKind::GenerateBlock: case SymbolKind::GenerateBlockArray: case SymbolKind::Genvar:
                        case SymbolKind::Modport: case SymbolKind::ClockingBlock:
                            if (auto region = regionOf(t.location); region && wholeIn != region && l.isUser(t.location))
                                if (once(*region, Unk, t.location)) inside[*region].unknown++;
                            break;
                        default: break;
                    }
                }
                visitDefault(t);
            }
        }
        void handle(const CaseStatement& c) {
            visitDefault(c);
            const auto* syntax = c.syntax && c.syntax->kind == syn::SyntaxKind::CaseStatement ? &c.syntax->as<syn::CaseStatementSyntax>() : nullptr;
            if (!syntax || !l.isUser(c.sourceRange.start())) return;
            // items a region switches off, or opens or closes inside
            for (const auto& item : syntax->items) {
                const auto span = spanOf(item->sourceRange());
                if (!span) continue;
                for (size_t i = 0; i < pragmas.regions.size(); i++) {
                    const auto& r = pragmas.regions[i];
                    const bool contains = span->lo < r.begin.offset() && span->hi > r.end;   // the region sits inside one item's body
                    if (wholeIn != i && relate(*span, i) != Rel::None && !contains && once(i, Item, item->getFirstToken().location()))
                        inside[i].caseItems++;
                }
            }
            const auto from = syntax->matchesOrInside ? syntax->matchesOrInside : syntax->closeParen;
            const size_t lo = from.location().offset() + from.rawText().size();
            const size_t hi = syntax->items.empty() ? syntax->endcase.location().offset() : syntax->items[0]->getFirstToken().location().offset();
            const auto buffer = from.location().buffer();
            std::string words;
            for (const auto& m : pragmas.marks)
                if ((m.kind == PragmaMark::FullCase || m.kind == PragmaMark::ParallelCase) && m.at.buffer().getId() == buffer.getId() &&
                    m.at.offset() >= lo && m.at.offset() < hi)
                    words += (words.empty() ? "" : " ") + m.spelling;
            // the attribute spellings `(* full_case *)`, `(* synthesis, parallel_case *)`
            for (const auto* attr : syntax->attributes)
                for (const auto* spec : attr->specs) {
                    const auto name = std::string(spec->name.valueText());
                    if (name == "full_case" || name == "parallel_case") words += (words.empty() ? "" : " ") + name;
                }
            if (words.empty() || !casesDone.insert({buffer.getId(), lo}).second) return;
            l.count();
            const auto facts = factsOf(l, c);
            const bool full = words.find("full_case") != std::string::npos, parallel = words.find("parallel_case") != std::string::npos;
            std::string detail;
            bool changesNothing = false;
            if (!facts.decided) detail = "the items are not plain constants, so whether it changes the logic is not decided here";
            else {
                if (full && facts.uncoveredCount) detail = "the items leave " + std::to_string(facts.uncoveredCount) + " selector value(s) uncovered (" + listOf(facts.uncovered) + (facts.uncoveredCount > 3 ? ", ..." : "") + "), which simulation keeps the old value for and synthesis treats as don't care";
                if (parallel && !facts.shared.empty()) detail += std::string(detail.empty() ? "" : "; ") + "items overlap on value(s) " + listOf(facts.shared) + ", where simulation takes the first item and synthesis builds no priority";
                if (detail.empty()) { changesNothing = true; detail = "the items cover every value without overlap, so it changes nothing here"; }
            }
            // A pragma on a case whose items cover every value exactly once changes nothing: advisory. One that leaves values uncovered
            // (a default assigned before the case does not help: the pragma makes them don't care) or that is not decided keeps the WARNING.
            l.report(c.sourceRange.start(), "case carries the synthesis pragma '" + words + "' (a comment or attribute): " +
                         (changesNothing ? "" : "synthesis builds different logic from simulation (") + detail + (changesNothing ? "" : ")"),
                     {.severity = changesNothing ? Severity::Info : Severity::Warning, .confidence = Confidence::Established,
                      .nextStep = changesNothing ? "Remove the pragma: a tool that ignores the comment builds the same logic."
                                                 : "Remove the pragma; write `unique case` or `priority case` (checked in simulation, built the same way), or complete the items."});
        }
    };

    void check(Lint& l) override {
        const auto pragmas = readUserPragmas(l.compilation());
        if (pragmas.marks.empty() && !pragmas.caseWords) return;
        Scan scan(l, pragmas);
        l.visitHardware(scan);
        for (size_t i = 0; i < pragmas.regions.size(); i++) {
            const auto& r = pragmas.regions[i];
            if (!l.isUser(r.begin)) continue;
            l.count();
            const auto it = scan.inside.find(i);
            const Inside empty;
            const auto& in = it == scan.inside.end() ? empty : it->second;
            // A closed region of procedures and declarations that drives nothing outside it and whose declarations nothing outside names is a
            // simulation checker: synthesis builds the design without it, and it changes nothing the design reads.
            if (r.closed && (in.always > 0 || in.declared > 0 || in.statements > 0) && in.continuous == 0 && in.initial == 0 && in.caseItems == 0 && in.parts == 0 &&
                in.instances == 0 && in.unknown == 0) {
                const auto& declared = scan.declaredIn[i];
                bool selfContained = !scan.uncertainIn.count(i);
                for (const auto* written : scan.writtenIn[i]) selfContained = selfContained && declared.count(written);
                for (const auto& [sym, at] : scan.references)
                    if (declared.count(sym) && scan.regionOf(at) != std::optional<size_t>(i)) selfContained = false;
                if (selfContained) {
                    const slang::SourceLocation endAt(r.begin.buffer(), r.end);
                    const auto from = l.loc(r.begin), to = l.loc(endAt);
                    l.context().design.simulationRegions.push_back({from.file, from.line, to.line});
                    l.report(r.begin, "the region switched off for synthesis by a translate_off pragma holds a simulation checker only (" + std::to_string(in.always) +
                                          " always block(s), " + std::to_string(in.declared) + " declaration(s)): it drives nothing that the rest of the design reads",
                             {.severity = Severity::Info, .confidence = Confidence::Established,
                              .nextStep = "No action: synthesis builds the design without the checker."});
                    continue;
                }
            }
            std::string what;
            auto add = [&](size_t n, const char* text) { if (n) what += (what.empty() ? "" : ", ") + std::to_string(n) + " " + text; };
            add(in.continuous, "continuous assignment(s)");
            add(in.always, "always block(s)");
            add(in.initial, "initial block(s) that assign design state");
            add(in.declared, "declaration(s) of design signals");
            add(in.statements, "assignment(s) to design state inside a procedure");
            add(in.caseItems, "case item(s)");
            add(in.parts, "part(s) of an expression");
            add(in.instances, "instance(s) that drive design signals");
            if (!what.empty()) {
                l.report(r.begin, "the region switched off for synthesis by a translate_off pragma holds " + what +
                                      ": simulation runs it, synthesis does not build it",
                         {.severity = Severity::Warning, .confidence = Confidence::Established,
                          .nextStep = "Move the logic out of the region or guard simulation-only code with a macro; whatever the region drives is undriven in synthesis."});
                continue;
            }
            // Nothing found that synthesis would miss. Quiet only when what is inside is proven simulation-only.
            if (in.unknown)
                l.report(r.begin, "the region switched off for synthesis by a translate_off pragma holds a call, an instance or a declaration that was not classified as simulation-only: whether synthesis builds the same logic is not decided",
                         {.severity = Severity::Warning, .confidence = Confidence::Unresolved,
                          .nextStep = "Keep only simulation code (messages, assertions, checkers) inside the region, or move the rest out."});
        }
        for (const auto& u : pragmas.unbalanced) {
            if (!l.isUser(u.mark.at)) continue;
            l.report(u.mark.at, "unbalanced synthesis pragma '" + u.mark.spelling + "': " + u.why,
                     {.severity = Severity::Warning, .confidence = Confidence::Established,
                      .nextStep = "Pair every translate_off with a translate_on in the same file; a tool reads the rest of the file as switched off."});
        }
    }
};

} // namespace

void demoteSimulationRegionFindings(Design& design, Report& report) {
    if (design.simulationRegions.empty()) return;
    for (auto& d : report.mutableDiagnostics()) {
        if (d.severity != Severity::Warning && d.severity != Severity::Error) continue;
        if (d.ruleId.rfind("LNT-", 0) != 0 || d.ruleId == "LNT-083" || d.ruleId == "LNT-001" || d.ruleId == "LNT-002" || d.ruleId == "LNT-003") continue;
        const bool inChecker = std::any_of(design.simulationRegions.begin(), design.simulationRegions.end(), [&](const Design::SimulationRegion& s) {
            return s.file == d.location.file && d.location.line >= s.first && d.location.line <= s.last;
        });
        if (!inChecker) continue;
        // Kept as INFO, never dropped: the original message stays, with the reason.
        d.severity = Severity::Info;
        d.message += " (inside a translate_off region that holds only a simulation checker: synthesis does not build this code)";
        d.nextStep = "No action unless the checker itself is wrong.";
        d.applyDefaultDisposition();
    }
}

std::vector<std::unique_ptr<Rule>> makeLintPragmaRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<SynthesisPragmaRule>());
    return v;
}

} // namespace sverity
