// Syntax-level lint rules over user sources; library units are excluded.
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/types/NetType.h"
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
// LNT-075: `default_nettype none` in effect at each module/interface/program
// ---------------------------------------------------------------------------------------------

/// The default net type in effect at each module, interface and program is slang's own record (the
/// preprocessor state when the declaration was parsed): it follows `include`d files, `resetall` and, with
/// --single-unit, the order of files. Library units (-v/-y) are read the same way.
class DefaultNetTypeRule final : public Rule {
public:
    std::string_view id() const override { return "LNT-075"; }
    void run(RuleContext& ctx) override {
        if (!designReady(ctx)) return;
        Sink sink(ctx.design.driver->sourceManager);
        std::vector<const slang::ast::DefinitionSymbol*> defs;
        for (const auto* sym : ctx.design.compilation->getDefinitions()) {
            if (sym->kind != slang::ast::SymbolKind::Definition) continue;
            const auto& def = sym->as<slang::ast::DefinitionSymbol>();
            if (!def.getSyntax() || def.getSyntax()->kind == syn::SyntaxKind::PackageDeclaration) continue;
            defs.push_back(&def);
        }
        auto locOf = [](const slang::ast::DefinitionSymbol* d) {
            return d->getSyntax()->as<syn::ModuleDeclarationSyntax>().getFirstToken().location();
        };
        std::sort(defs.begin(), defs.end(), [&](auto* x, auto* y) {
            auto lx = locOf(x), ly = locOf(y);
            return std::pair(lx.buffer().getId(), lx.offset()) < std::pair(ly.buffer().getId(), ly.offset());
        });
        slang::SourceLocation prev;
        for (const auto* d : defs) {
            auto loc = locOf(d);
            if (loc == prev) continue;
            prev = loc;
            sink.objects++;
            const auto& net = d->defaultNetType;
            if (net.netKind == slang::ast::NetType::Unknown) continue;
            const char* kind = d->definitionKind == slang::ast::DefinitionKind::Interface ? "interface"
                               : d->definitionKind == slang::ast::DefinitionKind::Program ? "program"
                                                                                          : "module";
            std::string msg = "default net type is '" + std::string(net.name) + "', not 'none', at " + kind + " '" +
                              std::string(d->name) + "'";
            if (d->syntaxTree && d->syntaxTree->isLibraryUnit) msg += " (library unit)";
            sink.add("LNT-075", std::move(msg), loc,
                     "add `default_nettype none` before the first module in this file, so that every net is "
                     "declared explicitly; restore the default with `default_nettype wire` after the file if needed");
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


// ---------------------------------------------------------------------------------------------
// LNT-086: positional port connection, positional parameter override and defparam
// ---------------------------------------------------------------------------------------------

/// Instantiations of modules, interfaces and programs (a user-defined primitive or a checker is not one: its ports are positional by
/// definition) that connect a port, or override a parameter, by position, and every `defparam`.
struct PositionalScan : syn::SyntaxVisitor<PositionalScan> {
    PositionalScan(Sink& s, const std::unordered_set<std::string_view>& d) : sink(s), definitions(d) {}
    Sink& sink;
    const std::unordered_set<std::string_view>& definitions;

    void handle(const syn::HierarchyInstantiationSyntax& n) {
        if (definitions.count(n.type.valueText())) {
            sink.objects++;
            const std::string module(n.type.valueText());
            if (n.parameters)
                for (const auto* p : n.parameters->parameters)
                    if (p->kind == syn::SyntaxKind::OrderedParamAssignment) {
                        sink.add("LNT-086", "parameters of '" + module + "' are overridden by position", n.parameters->getFirstToken().location(),
                                 "name each parameter (`#(.W(8))`): a positional list silently changes meaning when a parameter is added or reordered");
                        break;
                    }
            for (const auto* inst : n.instances)
                for (const auto* c : inst->connections)
                    if (c->kind == syn::SyntaxKind::OrderedPortConnection) {
                        const std::string name = inst->decl ? std::string(inst->decl->name.valueText()) : std::string();
                        sink.add("LNT-086", "ports of instance '" + name + "' of '" + module + "' are connected by position", inst->getFirstToken().location(),
                                 "connect each port by name (`.clk(clk)`): a positional list silently changes meaning when a port is added or reordered");
                        break;
                    }
        }
        visitDefault(n);
    }
    void handle(const syn::DefParamSyntax& n) {
        sink.objects++;
        sink.add("LNT-086", "defparam overrides a parameter from outside the instance", n.defparam.location(),
                 "pass the value in the instantiation (`#(.P(v))`): defparam is deprecated and its effect depends on elaboration order");
        visitDefault(n);
    }
};

class PositionalConnectionRule final : public Rule {
public:
    std::string_view id() const override { return "LNT-086"; }
    void run(RuleContext& ctx) override {
        if (!designReady(ctx)) return;
        Sink sink(ctx.design.driver->sourceManager);
        std::unordered_set<std::string_view> definitions;
        for (const auto* sym : ctx.design.compilation->getDefinitions())
            if (sym->kind == slang::ast::SymbolKind::Definition) definitions.insert(sym->name);
        for (const auto* t : userTrees(ctx.design)) {
            PositionalScan scan(sink, definitions);
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
    v.push_back(std::make_unique<PositionalConnectionRule>());
    return v;
}

} // namespace sverity
