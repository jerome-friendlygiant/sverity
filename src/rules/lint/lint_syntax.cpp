// Syntax-level lint rules over user sources; library units are excluded.
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/driver/Driver.h"
#include "slang/parsing/Token.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/text/SourceManager.h"
#include "rules/rules.h"

namespace sverity {
namespace {

namespace syn = slang::syntax;
using slang::BufferID;
using slang::SourceManager;

/// Collects the findings produced by one rule run; they are added to the report at the end.
struct Sink {
    explicit Sink(const SourceManager& s) : sm(s) {}
    const SourceManager& sm;
    std::vector<Diagnostic> findings;
    uint64_t objects = 0;

    void add(const char* ruleId, std::string message, slang::SourceLocation at, std::string nextStep) {
        Diagnostic d;
        d.ruleId = ruleId;
        d.severity = Severity::Info;
        d.confidence = Confidence::Established;
        d.message = std::move(message);
        d.nextStep = std::move(nextStep);
        d.location.file = sm.getFullPath(at.buffer()).generic_string();
        d.location.line = static_cast<uint32_t>(sm.getLineNumber(at));
        d.location.column = static_cast<uint32_t>(sm.getColumnNumber(at));
        findings.push_back(std::move(d));
    }
};

void flush(RuleContext& ctx, Sink& sink) {
    ctx.objectsEvaluated += sink.objects;
    for (auto& d : sink.findings) ctx.report.add(std::move(d));
}

bool designReady(RuleContext& ctx) {
    if (ctx.design.driver && ctx.design.compilation) return true;
    ctx.degradedReason = "design or compilation not available; syntax-level style checks did not run";
    return false;
}

/// Base for rules that cover only part of their catalog title: the reason is reported with coverage.
class PartialRule : public Rule {
public:
    PartialRule(const char* id, const char* reason) : id_(id), reason_(reason) {}
    std::string_view id() const override { return id_; }
    RuleState completeness() const override { return RuleState::Partial; }
    std::string partialReason() const override { return reason_; }

private:
    const char* id_;
    const char* reason_;
};

/// Syntax trees parsed from user sources. Library units (-v / -y, or trees of a non-default library)
/// are excluded.
std::vector<const syn::SyntaxTree*> userTrees(const Design& design) {
    std::vector<const syn::SyntaxTree*> out;
    for (const auto& t : design.compilation->getSyntaxTrees()) {
        if (!t || t->isLibraryUnit) continue;
        const auto* lib = t->getSourceLibrary();
        if (!lib || lib->isDefault) out.push_back(t.get());
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// LNT-075: explicit `default_nettype none` before each module/interface/program
// ---------------------------------------------------------------------------------------------

/// Gathers default_nettype directives and module-like declarations of one syntax tree. Directives are
/// trivia attached to tokens; their own source location tells which file they were written in.
struct NetTypeScan : syn::SyntaxVisitor<NetTypeScan> {
    struct Directive {
        uint32_t buf;
        size_t offset;
        bool none;
    };
    struct Decl {
        uint32_t buf;
        size_t offset;
        slang::SourceLocation loc;
        std::string kind;
        std::string name;
    };

    std::vector<Directive> directives;
    std::vector<Decl> decls;

    void handle(const syn::ModuleDeclarationSyntax& n) {
        if (n.kind != syn::SyntaxKind::PackageDeclaration && n.header) {
            auto loc = n.getFirstToken().location();
            const char* kind = n.kind == syn::SyntaxKind::InterfaceDeclaration ? "interface"
                               : n.kind == syn::SyntaxKind::ProgramDeclaration ? "program"
                                                                               : "module";
            decls.push_back({loc.buffer().getId(), loc.offset(), loc, kind, std::string(n.header->name.valueText())});
        }
        visitDefault(n);
    }

    void visitToken(slang::parsing::Token tok) {
        for (auto t : tok.trivia()) {
            if (t.kind != slang::parsing::TriviaKind::Directive) continue;
            const syn::SyntaxNode* s = t.syntax();
            if (!s || s->kind != syn::SyntaxKind::DefaultNetTypeDirective) continue;
            const auto& d = s->as<syn::DefaultNetTypeDirectiveSyntax>();
            auto loc = d.directive.location();
            directives.push_back({loc.buffer().getId(), loc.offset(), d.netType.rawText() == "none"});
        }
    }
};

class DefaultNetTypeRule final : public PartialRule {
public:
    DefaultNetTypeRule()
        : PartialRule("LNT-075",
                      "checks that each module, interface and program is preceded in its own source file by "
                      "`default_nettype none`; a directive written in an `include`d file or given on the command line "
                      "is not recognised, library units (-v/-y) are not checked, and the implicit-net policy (use of "
                      "implicit nets inside the design) is not checked") {}
    void run(RuleContext& ctx) override {
        if (!designReady(ctx)) return;
        Sink sink(ctx.design.driver->sourceManager);
        for (const auto* t : userTrees(ctx.design)) {
            NetTypeScan scan;
            t->root().visit(scan);
            for (const auto& m : scan.decls) {
                sink.objects++;
                const NetTypeScan::Directive* last = nullptr;
                for (const auto& d : scan.directives)
                    if (d.buf == m.buf && d.offset < m.offset && (!last || d.offset > last->offset)) last = &d;
                if (last && last->none) continue;
                std::string msg = last ? "default net type is not 'none' at " + m.kind + " '" + m.name + "'"
                                       : "no `default_nettype none` directive precedes " + m.kind + " '" + m.name +
                                             "' in its source file";
                sink.add("LNT-075", std::move(msg), m.loc,
                         "add `default_nettype none` before the first module in this file, so that every net is "
                         "declared explicitly; restore the default with `default_nettype wire` after the file if needed");
            }
        }
        flush(ctx, sink);
    }
};

// ---------------------------------------------------------------------------------------------
// LNT-076: plain `always` blocks should be always_comb / always_ff / always_latch
// ---------------------------------------------------------------------------------------------

/// True when the event expression contains an edge-sensitive term (posedge / negedge).
struct EdgeFinder : syn::SyntaxVisitor<EdgeFinder> {
    bool edge = false;
    void handle(const syn::SignalEventExpressionSyntax& e) {
        if (e.edge.kind == slang::parsing::TokenKind::PosEdgeKeyword ||
            e.edge.kind == slang::parsing::TokenKind::NegEdgeKeyword)
            edge = true;
        visitDefault(e);
    }
};

struct AlwaysScan : syn::SyntaxVisitor<AlwaysScan> {
    explicit AlwaysScan(Sink& s) : sink(s) {}
    Sink& sink;

    void handle(const syn::ProceduralBlockSyntax& n) {
        sink.objects++;
        if (n.kind == syn::SyntaxKind::AlwaysBlock) report(n);
        visitDefault(n);
    }

    void report(const syn::ProceduralBlockSyntax& n) {
        std::string msg = "plain 'always' block";
        std::string step = "use always_comb, always_ff or always_latch to state the intent of this block";
        const syn::TimingControlSyntax* tc = nullptr;
        if (n.statement->kind == syn::SyntaxKind::TimingControlStatement)
            tc = n.statement->as<syn::TimingControlStatementSyntax>().timingControl;
        if (tc && tc->kind == syn::SyntaxKind::ImplicitEventControl) {
            step = "use always_comb for this combinational block (@* sensitivity)";
        } else if (tc && tc->kind == syn::SyntaxKind::EventControlWithExpression) {
            const auto& ev = tc->as<syn::EventControlWithExpressionSyntax>();
            EdgeFinder finder;
            ev.expr->visit(finder);
            if (finder.edge)
                step = "use always_ff for this edge-triggered block (posedge/negedge sensitivity)";
            else
                step = "use always_comb for combinational logic, or always_latch for an intentional latch";
        }
        sink.add("LNT-076", std::move(msg), n.keyword.location(), std::move(step));
    }
};

class AlwaysStyleRule final : public PartialRule {
public:
    AlwaysStyleRule()
        : PartialRule("LNT-076",
                      "flags every plain 'always' block in user sources and suggests a replacement from its sensitivity "
                      "list; library units (-v/-y) are not checked, misuse of always_comb/always_ff/always_latch is not "
                      "checked (a separate semantic concern), and an always_latch choice for an intentional latch needs review") {}
    void run(RuleContext& ctx) override {
        if (!designReady(ctx)) return;
        Sink sink(ctx.design.driver->sourceManager);
        for (const auto* t : userTrees(ctx.design)) {
            AlwaysScan scan(sink);
            t->root().visit(scan);
        }
        flush(ctx, sink);
    }
};


} // namespace

std::vector<std::unique_ptr<Rule>> makeSyntaxLintRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<DefaultNetTypeRule>());
    v.push_back(std::make_unique<AlwaysStyleRule>());
    return v;
}

} // namespace sverity
