// STY family rules (coding style and source formatting). Every finding is Severity::Info with
// Confidence::Established: it follows directly from the stated style policy. All checks run over the
// user source files of the one elaborated design; library units (-v / -y) are excluded.
// Every rule here covers only part of its catalog title (the catalog is not updated by this change), so each
// reports RuleState::Partial with a precise reason (RFC section 3: never silently drop anything).
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

std::string join(const std::vector<std::string>& parts, const char* sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// STY-011: line length, tab characters, trailing whitespace (raw source text of user files)
// ---------------------------------------------------------------------------------------------

size_t maxLineLength(RuleContext& ctx) {
    const std::string opt = ctx.config.option("STY-011.max_line_length", "120");
    const bool digits = !opt.empty() && opt.size() < 10 &&
                        std::all_of(opt.begin(), opt.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
    if (digits) {
        size_t v = std::stoul(opt);
        if (v > 0) return v;
    }
    ctx.degradedReason = "invalid STY-011.max_line_length '" + opt + "' (expected a positive integer); default 120 used";
    return 120;
}

void checkLine(Sink& sink, BufferID buf, size_t lineOffset, std::string_view line, size_t maxLen) {
    sink.objects++;
    std::vector<std::string> problems, steps;
    size_t firstAt = std::string_view::npos;   // byte offset within the line of the first issue
    auto note = [&](size_t off) { firstAt = std::min(firstAt, off); };

    // Length in characters (UTF-8 code points, not bytes).
    size_t chars = 0, overAt = std::string_view::npos;
    for (size_t i = 0; i < line.size(); ++i) {
        if ((static_cast<unsigned char>(line[i]) & 0xC0) == 0x80) continue;
        if (chars == maxLen && overAt == std::string_view::npos) overAt = i;
        ++chars;
    }
    if (chars > maxLen) {
        problems.push_back("line is " + std::to_string(chars) + " characters, limit is " + std::to_string(maxLen));
        steps.push_back("wrap or shorten the line to at most " + std::to_string(maxLen) + " characters");
        note(overAt);
    }
    if (size_t tab = line.find('\t'); tab != std::string_view::npos) {
        problems.push_back("tab character");
        steps.push_back("replace tab characters with spaces");
        note(tab);
    }
    size_t trail = line.size();
    while (trail > 0 && (line[trail - 1] == ' ' || line[trail - 1] == '\t')) --trail;
    if (trail < line.size()) {
        problems.push_back("trailing whitespace");
        steps.push_back("remove trailing whitespace");
        note(trail);
    }
    if (problems.empty()) return;

    sink.add("STY-011", join(problems, "; "), slang::SourceLocation(buf, lineOffset + firstAt),
             join(steps, "; ") + ".");
}

class LineFormatRule final : public PartialRule {
public:
    LineFormatRule()
        : PartialRule("STY-011",
                      "checks line length (STY-011.max_line_length, default 120), tab characters and trailing whitespace "
                      "only; indentation is not checked and no formatter is provided") {}
    void run(RuleContext& ctx) override {
        if (!designReady(ctx)) return;
        const size_t maxLen = maxLineLength(ctx);
        const SourceManager& sm = ctx.design.driver->sourceManager;

        std::unordered_set<uint32_t> userRoots;
        for (const auto* t : userTrees(ctx.design)) {
            auto loc = t->root().getFirstToken().location();
            if (loc.valid()) userRoots.insert(loc.buffer().getId());
        }

        Sink sink(sm);
        for (BufferID buf : sm.getAllBuffers()) {
            // Buffers of a user file and of every file it includes (transitively) are user sources.
            BufferID root = buf;
            for (auto from = sm.getIncludedFrom(root); from.valid(); from = sm.getIncludedFrom(root))
                root = from.buffer();
            if (!userRoots.count(root.getId())) continue;

            std::string_view text = sm.getSourceText(buf);
            size_t pos = 0;
            while (pos < text.size()) {
                size_t nl = text.find('\n', pos);
                size_t end = nl == std::string_view::npos ? text.size() : nl;
                std::string_view line = text.substr(pos, end - pos);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                checkLine(sink, buf, pos, line, maxLen);
                if (nl == std::string_view::npos) break;
                pos = nl + 1;
            }
        }
        flush(ctx, sink);
    }
};

// ---------------------------------------------------------------------------------------------
// STY-006: explicit `default_nettype none` before each module/interface/program
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
        : PartialRule("STY-006",
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
                sink.add("STY-006", std::move(msg), m.loc,
                         "add `default_nettype none` before the first module in this file, so that every net is "
                         "declared explicitly; restore the default with `default_nettype wire` after the file if needed");
            }
        }
        flush(ctx, sink);
    }
};

// ---------------------------------------------------------------------------------------------
// STY-007: plain `always` blocks should be always_comb / always_ff / always_latch
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
        sink.add("STY-007", std::move(msg), n.keyword.location(), std::move(step));
    }
};

class AlwaysStyleRule final : public PartialRule {
public:
    AlwaysStyleRule()
        : PartialRule("STY-007",
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
// STY-004: wildcard (.*) port connections
// ---------------------------------------------------------------------------------------------

struct WildcardScan : syn::SyntaxVisitor<WildcardScan> {
    explicit WildcardScan(Sink& s) : sink(s) {}
    Sink& sink;
    std::string moduleName;
    std::string instanceName;

    void handle(const syn::HierarchyInstantiationSyntax& n) {
        moduleName = std::string(n.type.valueText());
        visitDefault(n);
    }
    void handle(const syn::HierarchicalInstanceSyntax& n) {
        sink.objects++;
        instanceName = n.decl ? std::string(n.decl->name.valueText()) : std::string();
        visitDefault(n);
    }
    void handle(const syn::WildcardPortConnectionSyntax& n) {
        std::string where = instanceName.empty() ? moduleName : moduleName + "' instance '" + instanceName;
        sink.add("STY-004", "wildcard port connection '.*' on '" + where + "'", n.dot.location(),
                 "replace '.*' with explicit named connections (.port(signal)) so that every connection is reviewable");
        visitDefault(n);
    }
};

class WildcardPortRule final : public PartialRule {
public:
    WildcardPortRule()
        : PartialRule("STY-004",
                      "reports only wildcard '.*' port connections in user sources; explicit named, implicit '.name' and "
                      "positional port connections are not classified because the positional-connection policy is not "
                      "specified, and library units (-v/-y) are not checked") {}
    void run(RuleContext& ctx) override {
        if (!designReady(ctx)) return;
        Sink sink(ctx.design.driver->sourceManager);
        for (const auto* t : userTrees(ctx.design)) {
            WildcardScan scan(sink);
            t->root().visit(scan);
        }
        flush(ctx, sink);
    }
};

// ---------------------------------------------------------------------------------------------
// STY-005: named versus positional parameter overrides
// ---------------------------------------------------------------------------------------------

struct ParamScan : syn::SyntaxVisitor<ParamScan> {
    explicit ParamScan(Sink& s) : sink(s) {}
    Sink& sink;
    const void* instParams = nullptr;   // parameter list of the instantiation being walked
    std::string moduleName;

    void handle(const syn::HierarchyInstantiationSyntax& n) {
        moduleName = std::string(n.type.valueText());
        instParams = n.parameters ? static_cast<const void*>(n.parameters) : nullptr;
        visitDefault(n);
        instParams = nullptr;
    }
    void handle(const syn::ParameterValueAssignmentSyntax& n) {
        sink.objects++;
        bool positional = false;
        for (const auto* p : n.parameters)
            if (p && p->kind == syn::SyntaxKind::OrderedParamAssignment) positional = true;
        if (positional) {
            bool isInstance = instParams == static_cast<const void*>(&n);
            std::string what = isInstance ? "instance of '" + moduleName + "'" : "parameter specialization";
            sink.add("STY-005", "positional parameter override in " + what, n.hash.location(),
                     "use named parameter assignments, #(.NAME(value)), so the override does not depend on parameter order");
        }
        visitDefault(n);
    }
};

class ParamOverrideRule final : public PartialRule {
public:
    ParamOverrideRule()
        : PartialRule("STY-005",
                      "reports positional '#(...)' parameter lists in user sources (instantiations, checker, config and "
                      "type specialization); defparam statements are not classified and library units (-v/-y) are not "
                      "checked") {}
    void run(RuleContext& ctx) override {
        if (!designReady(ctx)) return;
        Sink sink(ctx.design.driver->sourceManager);
        for (const auto* t : userTrees(ctx.design)) {
            ParamScan scan(sink);
            t->root().visit(scan);
        }
        flush(ctx, sink);
    }
};

// ---------------------------------------------------------------------------------------------
// STY-012: module/endmodule and named begin/end label consistency
// ---------------------------------------------------------------------------------------------

struct EndLabelScan : syn::SyntaxVisitor<EndLabelScan> {
    explicit EndLabelScan(Sink& s) : sink(s) {}
    Sink& sink;

    /// Compares a construct's name with its end label. Both names must be present and equal.
    void check(const char* what, const std::string& begin, const syn::NamedBlockClauseSyntax* endName,
               slang::SourceLocation endAt, bool hasBegin) {
        std::string endLabel = endName ? std::string(endName->name.valueText()) : std::string();
        if (!endName && !hasBegin) return;
        if (hasBegin && !endName) {
            sink.add("STY-012", std::string(what) + " '" + begin + "' has no matching end label ': " + begin + "'", endAt,
                     "add ': " + begin + "' after the end keyword");
        } else if (!hasBegin && endName) {
            sink.add("STY-012", std::string(what) + " end label ': " + endLabel + "' has no begin label", endAt,
                     "remove the end label or name the opening keyword with the same label");
        } else if (endLabel != begin) {
            sink.add("STY-012", std::string(what) + " end label ': " + endLabel + "' does not match ': " + begin + "'", endAt,
                     "make the end label match the declaration or block name");
        }
    }

    void handle(const syn::ModuleDeclarationSyntax& n) {
        sink.objects++;
        if (n.header) {
            const char* kind = n.kind == syn::SyntaxKind::InterfaceDeclaration ? "interface"
                               : n.kind == syn::SyntaxKind::ProgramDeclaration ? "program"
                               : n.kind == syn::SyntaxKind::PackageDeclaration ? "package"
                                                                               : "module";
            std::string name(n.header->name.valueText());
            std::string what = std::string(kind);
            if (!n.blockName) {
                sink.add("STY-012", what + " '" + name + "' has no end label ': " + name + "' on its end keyword",
                         n.endmodule.location(), "add ': " + name + "' after the end keyword");
            } else if (n.blockName->name.valueText() != name) {
                sink.add("STY-012", what + " end label ': " + std::string(n.blockName->name.valueText()) +
                                        "' does not match its name '" + name + "'",
                         n.endmodule.location(), "make the end label match the " + what + " name");
            }
        }
        visitDefault(n);
    }

    void handle(const syn::BlockStatementSyntax& n) {
        sink.objects++;
        std::string begin = n.blockName ? std::string(n.blockName->name.valueText()) : std::string();
        check("block", begin, n.endBlockName, n.end.location(), n.blockName != nullptr);
        visitDefault(n);
    }
};

class EndLabelRule final : public PartialRule {
public:
    EndLabelRule()
        : PartialRule("STY-012",
                      "checks end labels of module, interface, program and package declarations and of named begin/end "
                      "and fork/join blocks in user sources; end labels of generate blocks, functions, tasks, classes, "
                      "checkers and sequences are not checked, and library units (-v/-y) are not checked") {}
    void run(RuleContext& ctx) override {
        if (!designReady(ctx)) return;
        Sink sink(ctx.design.driver->sourceManager);
        for (const auto* t : userTrees(ctx.design)) {
            EndLabelScan scan(sink);
            t->root().visit(scan);
        }
        flush(ctx, sink);
    }
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeStyleRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<WildcardPortRule>());     // STY-004
    v.push_back(std::make_unique<ParamOverrideRule>());    // STY-005
    v.push_back(std::make_unique<DefaultNetTypeRule>());   // STY-006
    v.push_back(std::make_unique<AlwaysStyleRule>());      // STY-007
    v.push_back(std::make_unique<LineFormatRule>());       // STY-011
    v.push_back(std::make_unique<EndLabelRule>());         // STY-012
    return v;
}

} // namespace sverity
