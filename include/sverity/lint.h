// Public helpers for AST lint rules (built-in LNT rules and user-written custom rules). A rule is one class + one slang ASTVisitor; the helper turns slang
// locations into SVerity findings, skips library units (-v/-y) and keeps the coverage accounting. Adding a rule:
//   1. a catalog entry, 2. a class deriving from LintRule (see src/rules/lint/), 3. one line in makeLintRules(); embedders use registerRule().
#pragma once
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "sverity/rule.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
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
};

/// Per-run helper: location conversion, user-code filter and finding construction.
class Lint {
public:
    using Opts = LintOpts;
    Lint(RuleContext& ctx, std::string ruleId) : ctx_(ctx), id_(std::move(ruleId)), sm_(*ctx.design.compilation->getSourceManager()) {
        for (const auto& t : ctx.design.compilation->getSyntaxTrees())
            if (t && !t->isLibraryUnit) userBuffers_.insert(t->root().sourceRange().start().buffer().getId());
    }

    /// True when the location lies in a user source file (library units and unknown locations are excluded).
    bool isUser(slang::SourceLocation l) const {
        if (!l.valid()) return false;
        l = sm_.getFullyOriginalLoc(l);
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


    /// Adds a finding at `at`. Findings in library units are dropped by isUser(), never by severity.
    void report(slang::SourceLocation at, std::string message, LintOpts o = {}) {
        SourceLocation loc;
        slang::SourceLocation l = sm_.getFullyOriginalLoc(at);
        if (l.valid()) {
            loc.file = sm_.getFullPath(l.buffer()).generic_string();
            loc.line = static_cast<uint32_t>(sm_.getLineNumber(l));
            loc.column = static_cast<uint32_t>(sm_.getColumnNumber(l));
        }
        reportAt(std::move(loc), std::move(message), std::move(o));
    }
    /// Same, for a location that is already a SVerity location (e.g. from the shared model).
    void reportAt(SourceLocation loc, std::string message, LintOpts o = {}) {
        Diagnostic d;
        d.ruleId = id_;
        d.severity = o.severity;
        d.confidence = o.confidence;
        d.proofState = o.proof;
        d.message = std::move(message);
        d.nextStep = std::move(o.nextStep);
        d.hierarchy = std::move(o.hierarchy);
        d.signals = std::move(o.signals);
        d.evidence = std::move(o.evidence);
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
        lint.compilation().getRoot().visit(v);
    }

private:
    std::string id_, gap_;
    bool upstream_;
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
    std::vector<const slang::ast::NamedValueExpression*> refs;
    void handle(const slang::ast::NamedValueExpression& e) {
        if (auto* s = e.getSymbolReference()) { syms.insert(s); refs.push_back(&e); }
    }
    void handle(const slang::ast::AssignmentExpression& a) {
        if (a.isCompound() || a.left().kind != slang::ast::ExpressionKind::NamedValue) a.left().visit(*this);
        a.right().visit(*this);
    }
};

} // namespace sverity::lint
