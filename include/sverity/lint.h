// Public helpers for AST lint rules (built-in LNT rules and user-written custom rules). A rule is one class + one slang ASTVisitor; the helper turns slang
// locations into SVerity findings, skips library units (-v/-y) and keeps the coverage accounting. Adding a rule:
//   1. a catalog entry, 2. a class deriving from LintRule (see src/rules/lint/), 3. one line in makeLintRules(); embedders use registerRule().
#pragma once
#include <set>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "sverity/rule.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/EvalContext.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"

namespace sverity::lint {

using slang::ast::VisitFlags;

struct LintOpts {
    Severity severity = Severity::Warning;
    Confidence confidence = Confidence::Established;
    ProofState proof = ProofState::NotApplicable;
    std::string nextStep;
    std::string hierarchy;
    std::vector<std::string> signals;
    std::vector<Evidence> evidence;
    std::string groupingUnit;
    uint32_t groupingProcedureOrdinal = 0;
    std::string identityKey;
    std::string obligation;   // obligation predicate id declared in the catalog (see Diagnostic::obligation)
};

/// Per-run helper: location conversion, user-code filter and finding construction.
class Lint {
public:
    using Opts = LintOpts;
    Lint(RuleContext& ctx, std::string ruleId) : ctx_(ctx), id_(std::move(ruleId)), sm_(*ctx.design.compilation->getSourceManager()) {
        // Every source file of a non-library tree: with --single-unit one tree holds all of them.
        for (const auto& t : ctx.design.compilation->getSyntaxTrees())
            if (t && !t->isLibraryUnit) {
                userBuffers_.insert(t->root().sourceRange().start().buffer().getId());
                for (auto id : t->getSourceBufferIds()) userBuffers_.insert(id.getId());
            }
    }

    /// True when the use site of the location lies in a user source file (library units and unknown locations are excluded).
    /// A macro's use site decides: a macro from a library header used in user code is user code, and the reverse is not.
    bool isUser(slang::SourceLocation l) const {
        if (!l.valid()) return false;
        l = sm_.getFullyExpandedLoc(l);
        return userBuffers_.count(l.buffer().getId()) != 0;
    }
    void count(uint64_t n = 1) { ctx_.objectsEvaluated += n; }

    std::string option(const std::string& name, const std::string& def) const { return ctx_.config.option(id_ + "." + name, def); }
    slang::ast::Compilation& compilation() const { return *ctx_.design.compilation; }
    const slang::SourceManager& sm() const { return sm_; }
    RuleContext& context() { return ctx_; }
    /// Constant value of `e` if it can be evaluated outside any procedural context; a null value otherwise.
    slang::ConstantValue evalConst(const slang::ast::Expression& e) const {
        slang::ast::ASTContext ac(ctx_.design.compilation->getRoot(), slang::ast::LookupLocation::max);
        slang::ast::EvalContext ec(ac);
        return e.eval(ec);
    }
    /// As evalConst, and a hierarchical reference to a parameter or enum value folds too. slang refuses such a reference in a
    /// constant expression (CompilationFlags::AllowHierarchicalConst), but the value read here is the elaborated one, and
    /// ConfigParam is the context flag that lifts that refusal for slang's own config-parameter evaluation. Used where a rule
    /// needs the bits a select of a hierarchical constant holds.
    slang::ConstantValue evalElaborated(const slang::ast::Expression& e) const {
        slang::ast::ASTContext ac(ctx_.design.compilation->getRoot(), slang::ast::LookupLocation::max, slang::ast::ASTFlags::ConfigParam);
        slang::ast::EvalContext ec(ac);
        return e.eval(ec);
    }


    /// Visits the elaborated design with `v`: every root member except instance bodies slang elaborated only for
    /// diagnostics (an unreferenced definition, flagged Uninstantiated). Those are not hardware of this run and are only
    /// partly bound, so a finding there would describe nothing.
    template <typename V>
    void visitHardware(V& v) const {
        for (const auto& member : compilation().getRoot().members()) {
            if (const auto* inst = member.as_if<slang::ast::InstanceSymbol>();
                inst && inst->body.flags.has(slang::ast::InstanceFlags::Uninstantiated))
                continue;
            member.visit(v);
        }
    }
    /// A SVerity location for a slang one (empty when it has no file). A construct written by a macro is located at the
    /// macro's use site, so each use is its own finding; the macro body is kept as evidence by report().
    SourceLocation loc(slang::SourceLocation at) const {
        SourceLocation loc;
        slang::SourceLocation l = sm_.getFullyExpandedLoc(at);
        if (l.valid()) {
            loc.file = sm_.getFullPath(l.buffer()).generic_string();
            loc.line = static_cast<uint32_t>(sm_.getLineNumber(l));
            loc.column = static_cast<uint32_t>(sm_.getColumnNumber(l));
        }
        return loc;
    }
    /// Adds a finding at `at`. Findings in library units are dropped by isUser(), never by severity. A construct written in a
    /// macro body keeps the body's location as evidence ("macro_definition"); a macro argument is user text, not a body.
    void report(slang::SourceLocation at, std::string message, LintOpts o = {}) {
        if (sm_.isMacroLoc(at) && !sm_.isMacroArgLoc(at)) {
            slang::SourceLocation body = sm_.getFullyOriginalLoc(at);
            if (body.valid() && body != sm_.getFullyExpandedLoc(at)) {
                std::string name(sm_.getMacroName(at));
                o.evidence.push_back({"macro_definition", name.empty() ? "construct written in a macro" : "construct written by macro '" + name + "'", loc(body)});
            }
        }
        reportAt(loc(at), std::move(message), std::move(o));
    }
    /// Same, for a location that is already a SVerity location (e.g. from the shared model).
    void reportAt(SourceLocation loc, std::string message, LintOpts o = {}) {
        Diagnostic d;
        d.ruleId = id_;
        d.severity = o.severity;
        d.confidence = o.confidence;
        d.proofState = o.proof;
        d.message = std::move(message);
        d.identityKey = std::move(o.identityKey);
        d.obligation = std::move(o.obligation);
        d.nextStep = std::move(o.nextStep);
        d.hierarchy = std::move(o.hierarchy);
        d.signals = std::move(o.signals);
        d.evidence = std::move(o.evidence);
        d.groupingUnit = std::move(o.groupingUnit);
        d.groupingProcedureOrdinal = o.groupingProcedureOrdinal;
        d.location = std::move(loc);
        ctx_.report.add(std::move(d));
    }
    /// Convenience: report on a symbol, filling the hierarchy.
    void report(const slang::ast::Symbol& sym, std::string message, LintOpts o = {}) {
        if (o.hierarchy.empty()) o.hierarchy = sym.getHierarchicalPath();
        report(sym.location, std::move(message), std::move(o));
    }

private:
    RuleContext& ctx_;
    std::string id_;
    const slang::SourceManager& sm_;
    std::unordered_set<uint32_t> userBuffers_;
};

/// Base class of the native lint rules. `gap` empty = the rule covers its whole catalog title.
class LintRule : public Rule {
public:
    /// `upstream`: part of the rule is slang's own diagnostics (mapped in frontend/slang_rule_map.cpp); the rule is then
    /// degraded when slang diagnostics were limited or the design has elaboration errors.
    explicit LintRule(const char* id, const char* gap = "", bool upstream = false) : id_(id), gap_(gap), upstream_(upstream) {}
    bool upstreamBacked() const final { return upstream_; }
    std::string_view id() const final { return id_; }
    RuleState completeness() const final { return gap_.empty() ? RuleState::Executed : RuleState::Partial; }
    std::string partialReason() const final { return gap_; }
    void run(RuleContext& ctx) final {
        if (!ctx.design.compilation) {
            ctx.degradedReason = "no elaborated design";
            return;
        }
        if (upstream_) {
            for (auto& sc : ctx.report.scopes())
                if (sc.scope == "upstream-diagnostics" && sc.coverage == AnalysisCoverage::Incomplete)
                    ctx.degradedReason = "slang diagnostics were disabled or limited by command-line options";
            if (ctx.design.numUpstreamErrors > 0 && ctx.degradedReason.empty())
                ctx.degradedReason = "design has elaboration errors; slang checks may be skipped on erroneous code";
        }
        Lint lint(ctx, id_);
        check(lint);
    }

protected:
    virtual void check(Lint& lint) = 0;
    /// Visits every canonical instance body once with `V` (an ASTVisitor<V, VisitFlags::AllCanonical>).
    template <typename V, typename... Args>
    static void visitDesign(Lint& lint, Args&&... args) {
        V v(lint, std::forward<Args>(args)...);
        lint.visitHardware(v);
    }

private:
    std::string id_, gap_;
    bool upstream_;
};

/// AST visitor for semantic lint checks. Slang visits inactive generate scopes by default so
/// tools can inventory them (LNT-051). Ordinary semantic checks must ignore those scopes,
/// including nested scopes whose own flag may look active but whose ancestor is inactive.
template <typename Derived, VisitFlags Flags = VisitFlags::AllCanonical>
class HardwareVisitor : public slang::ast::ASTVisitor<Derived, Flags> {
public:
    template <typename T>
    void visit(const T& node) {
        if constexpr (std::is_base_of_v<slang::ast::Scope, T>) {
            if (inactive(&static_cast<const slang::ast::Scope&>(node))) return;
        }
        else if constexpr (std::is_base_of_v<slang::ast::Symbol, T>) {
            if (inactive(node.getParentScope())) return;
        }
        slang::ast::ASTVisitor<Derived, Flags>::visit(node);
    }

private:
    static bool inactive(const slang::ast::Scope* scope) {
        for (; scope; scope = scope->asSymbol().getParentScope())
            if (scope->isUninstantiated()) return true;
        return false;
    }
};


// ---- AST helpers shared by the lint rules (one definition; rules must not re-implement them) -------------------------------

/// True when the symbol is declared at module / generate / package / compilation-unit level (state), not inside a procedure.
inline bool isModuleLevel(const slang::ast::Symbol& s) {
    auto* sc = s.getParentScope();
    if (!sc) return false;
    auto k = sc->asSymbol().kind;
    return k == slang::ast::SymbolKind::InstanceBody || k == slang::ast::SymbolKind::GenerateBlock ||
           k == slang::ast::SymbolKind::CompilationUnit || k == slang::ast::SymbolKind::Package;
}

/// The instance body (module, interface, program) that encloses a symbol; null at compilation-unit level.
inline const slang::ast::InstanceBodySymbol* enclosingBody(const slang::ast::Symbol& s) {
    for (const slang::ast::Scope* sc = s.getParentScope(); sc; sc = sc->asSymbol().getParentScope())
        if (sc->asSymbol().kind == slang::ast::SymbolKind::InstanceBody) return &sc->asSymbol().as<slang::ast::InstanceBodySymbol>();
    return nullptr;
}

/// Root symbols written by an assignment target: a variable, a select or member of one, or every operand of a concatenation.
inline void lvalueSymbols(const slang::ast::Expression& e, std::vector<const slang::ast::Symbol*>& out) {
    using slang::ast::ExpressionKind;
    switch (e.kind) {
        case ExpressionKind::Concatenation:
            for (auto* op : e.as<slang::ast::ConcatenationExpression>().operands()) lvalueSymbols(*op, out);
            return;
        case ExpressionKind::MemberAccess:
            lvalueSymbols(e.as<slang::ast::MemberAccessExpression>().value(), out);
            return;
        case ExpressionKind::ElementSelect:
            lvalueSymbols(e.as<slang::ast::ElementSelectExpression>().value(), out);
            return;
        case ExpressionKind::RangeSelect:
            lvalueSymbols(e.as<slang::ast::RangeSelectExpression>().value(), out);
            return;
        case ExpressionKind::Conversion:
            lvalueSymbols(e.as<slang::ast::ConversionExpression>().operand(), out);
            return;
        case ExpressionKind::Streaming:   // `{<<{r}} = d`: every stream operand is a target
            for (const auto& stream : e.as<slang::ast::StreamingConcatenationExpression>().streams()) lvalueSymbols(*stream.operand, out);
            return;
        default:
            if (auto* s = e.getSymbolReference()) out.push_back(s);
    }
}
inline std::vector<const slang::ast::Symbol*> lvalueSymbols(const slang::ast::Expression& e) {
    std::vector<const slang::ast::Symbol*> v;
    lvalueSymbols(e, v);
    return v;
}

/// Every assignment expression under a node (statements, expressions, continuous assigns).
struct Assigns : slang::ast::ASTVisitor<Assigns, VisitFlags::AllGood> {
    std::vector<const slang::ast::AssignmentExpression*> list;
    void handle(const slang::ast::AssignmentExpression& a) {
        list.push_back(&a);
        visitDefault(a);
    }
};

/// Symbols read under a node. A plain whole-variable assignment target is not a read; compound targets and select
/// index expressions are.
struct Reads : slang::ast::ASTVisitor<Reads, VisitFlags::AllGood> {
    std::set<const slang::ast::Symbol*> syms;
    std::vector<const slang::ast::Expression*> refs;
    void handle(const slang::ast::NamedValueExpression& e) {
        if (auto* s = e.getSymbolReference()) { syms.insert(s); refs.push_back(&e); }
    }
    void handle(const slang::ast::HierarchicalValueExpression& e) {
        if (auto* s = e.getSymbolReference()) { syms.insert(s); refs.push_back(&e); }
    }
    void targetReads(const slang::ast::Expression& e) {
        using namespace slang::ast;
        switch (e.kind) {
            case ExpressionKind::NamedValue:
            case ExpressionKind::HierarchicalValue: return;
            case ExpressionKind::ElementSelect: {
                const auto& select = e.as<ElementSelectExpression>();
                targetReads(select.value());
                select.selector().visit(*this);
                return;
            }
            case ExpressionKind::RangeSelect: {
                const auto& select = e.as<RangeSelectExpression>();
                targetReads(select.value());
                select.left().visit(*this);
                select.right().visit(*this);
                return;
            }
            case ExpressionKind::MemberAccess:
                targetReads(e.as<MemberAccessExpression>().value());
                return;
            case ExpressionKind::Concatenation:
                for (auto* operand : e.as<ConcatenationExpression>().operands()) targetReads(*operand);
                return;
            default: e.visit(*this); return; // Unclassified targets conservatively retain their reads.
        }
    }
    void handle(const slang::ast::AssignmentExpression& a) {
        if (a.isCompound()) a.left().visit(*this);
        else targetReads(a.left());
        a.right().visit(*this);
    }
};

} // namespace sverity::lint
