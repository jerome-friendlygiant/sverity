#include <set>
#include <algorithm>
#include "sverity/frontend.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Scope.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/types/Type.h"
#include "slang/diagnostics/DiagnosticClient.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/driver/Driver.h"
#include "slang/diagnostics/AllDiags.h"
#include "slang/util/OS.h"
#include "slang/text/SourceManager.h"
#include "slang/util/VersionInfo.h"

namespace sverity {

Design::Design() = default;
Design::~Design() = default;

namespace {

SourceLocation toLoc(const slang::SourceManager& sm, slang::SourceLocation l) {
    SourceLocation out;
    if (!l.valid() || l.buffer() == slang::SourceLocation::NoLocation.buffer()) return out;
    try {
        out.file = sm.getFullPath(l.buffer()).generic_string();
        if (out.file.empty()) out.file = std::string(sm.getFileName(l));
        out.line = static_cast<uint32_t>(sm.getLineNumber(l));
        out.column = static_cast<uint32_t>(sm.getColumnNumber(l));
    } catch (...) {
        out = SourceLocation{};
    }
    return out;
}

/// The types a slang diagnostic carries as arguments, in order (a string or number argument is skipped).
std::vector<const slang::ast::Type*> typeArguments(const slang::Diagnostic& diag) {
    std::vector<const slang::ast::Type*> types;
    for (const auto& arg : diag.args)
        if (const auto* c = std::get_if<slang::Diagnostic::CustomArgType>(&arg))
            if (const auto* t = std::any_cast<const slang::ast::Type*>(&c->second)) types.push_back(*t);
    return types;
}

/// The element type of a packed array, through every dimension.
const slang::ast::Type& packedElement(const slang::ast::Type& t) {
    const slang::ast::Type* p = &t.getCanonicalType();
    while (p->isPackedArray() && p->getArrayElementType()) p = &p->getArrayElementType()->getCanonicalType();
    return *p;
}

/// An integer, bit or logic vector of any dimensions: not an enum, struct or union, whose type is part of what the author meant.
bool plainBits(const slang::ast::Type& t) {
    const auto k = packedElement(t).kind;
    return t.isIntegral() && (k == slang::ast::SymbolKind::PredefinedIntegerType || k == slang::ast::SymbolKind::ScalarType);
}

/// Plain bits, or a packed struct all of whose fields are (through nested structs and arrays): its layout is the vector's.
bool plainBitsOrStruct(const slang::ast::Type& t) {
    const auto& e = packedElement(t);
    if (e.kind == slang::ast::SymbolKind::PackedStructType) {
        for (const auto& field : e.as<slang::ast::PackedStructType>().membersOfType<slang::ast::FieldSymbol>())
            if (!plainBitsOrStruct(field.getType())) return false;
        return true;
    }
    return plainBits(t);
}

/// slang's packed-array-conv for two packed types of the same total width, no enum in the destination and no 4-state value
/// into a 2-state one: the same bits under another name, which synthesis builds as written.
bool reinterpretsBits(const slang::Diagnostic& diag) {
    const auto types = typeArguments(diag);
    if (types.size() < 2) return false;
    const auto& from = *types[0];
    const auto& to = *types[1];
    return from.isIntegral() && to.isIntegral() && from.getBitWidth() == to.getBitWidth() && plainBitsOrStruct(to) &&
           !(from.isFourState() && !to.isFourState());
}

/// slang's case-type for an enum against a plain vector of the same width: the item compares bit for bit.
bool enumAgainstPlainVector(const slang::Diagnostic& diag) {
    const auto types = typeArguments(diag);
    if (types.size() < 2) return false;
    const auto& a = *types[0];
    const auto& b = *types[1];
    return a.isIntegral() && b.isIntegral() && a.getBitWidth() == b.getBitWidth() &&
           ((a.isEnum() && plainBits(b)) || (b.isEnum() && plainBits(a)));
}

/// Receives every diagnostic slang issues and converts it to a SVerity Diagnostic. Nothing is
/// filtered here: ignored/notes never reach clients by slang's own design, everything else does.
class CaptureClient : public slang::DiagnosticClient {
public:
    CaptureClient(Report& r, Design& d, bool compat) : out(r), design(d), compat(compat) {}
    bool compat;   // --compat chose slang's lowered severities on purpose (and is disclosed as INCOMPLETE)

    void report(const slang::ReportedDiagnostic& rd) override {
        using slang::DiagnosticSeverity;
        if (rd.severity == DiagnosticSeverity::Ignored) return;   // cannot happen via the engine
        Diagnostic d;
        const auto& sm = *sourceManager;
        std::string opt(engine->getOptionName(rd.originalDiagnostic.code));
        d.origin = "slang";
        d.upstreamCode = opt.empty() ? std::string(slang::toString(rd.originalDiagnostic.code)) : opt;
        if (rd.originalDiagnostic.code == slang::diag::TooManyErrors) sawTooManyErrors = true;
        d.upstreamSeverity = std::string(getSeverityString(rd.severity));
        // slang raises these to Error by default because each is proven wrong or unbuildable hardware; a -W option,
        // -Wdefault, --compat or an in-source pragma can lower them back to Warning. The defect is the same, so it
        // stays ERROR (the lowered upstream severity is kept). Suppressing them entirely is disclosed separately.
        static const std::set<slang::DiagCode> provenErrors = {
            slang::diag::IndexOOB, slang::diag::RangeOOB, slang::diag::RangeWidthOOB, slang::diag::MultipleContAssigns,
            slang::diag::MultipleAlwaysAssigns, slang::diag::MixedVarAssigns, slang::diag::DuplicateDefinition,
            slang::diag::Redefinition, slang::diag::RedefinitionDifferentType, slang::diag::BadProceduralForce,
            slang::diag::ImplicitNamedPortTypeMismatch};
        // The rest of slang's default errors (CompatSettings::configureDiagnostics) also stay ERROR unless --compat asked
        // for another tool's leniency.
        static const std::set<slang::DiagCode> defaultErrors = {
            slang::diag::UnknownSystemName, slang::diag::NonstandardHierarchicalCross, slang::diag::NonstandardStringConcat,
            slang::diag::NonstandardInside, slang::diag::MisplacedTrailingSeparator, slang::diag::InitializerRequired,
            slang::diag::ConcatWithStringInt, slang::diag::SplitDistWeightOp, slang::diag::DPIPureTask,
            slang::diag::SpecifyPathConditionExpr, slang::diag::SolveBeforeDisallowed, slang::diag::ForeachCallExpr,
            slang::diag::DynamicNotProcedural, slang::diag::QualifiersOnOutOfBlock, slang::diag::MemberImplNotFound,
            slang::diag::PackageImportInClass, slang::diag::BareAssociativePattern, slang::diag::DigitsLeadingUnderscore,
            slang::diag::RefArgAutomaticFunc, slang::diag::RangeSelectReversed, slang::diag::NonstandardConstraintBlock,
            slang::diag::FormatEmptyArg, slang::diag::FormatTooManyArgs, slang::diag::ClockVarTargetAssign,
            slang::diag::CrossIdentInBinsof, slang::diag::RefArgForkJoin, slang::diag::CannotIndexScalar,
            slang::diag::StringConstraintExpr, slang::diag::VirtualArgNameMismatch, slang::diag::ParameterDoesNotExist};
        const auto code = rd.originalDiagnostic.code;
        const auto severity = rd.severity == DiagnosticSeverity::Warning &&
                                      (provenErrors.count(code) || (!compat && defaultErrors.count(code)))
                                  ? DiagnosticSeverity::Error
                                  : rd.severity;
        switch (severity) {
            case DiagnosticSeverity::Note: d.severity = Severity::Info; break;
            case DiagnosticSeverity::Warning: d.severity = Severity::Warning; design.numUpstreamWarnings++; break;
            case DiagnosticSeverity::Error:
                d.severity = Severity::Error;
                // An index or range select out of its object is a legal construct whose value the LRM defines (X, or no write): slang reports it
                // as an error, the finding stays an ERROR at the site (LNT-006), and the design around it is still analysed.
                if (!(code == slang::diag::IndexOOB || code == slang::diag::RangeOOB || code == slang::diag::RangeWidthOOB))
                    design.numUpstreamErrors++;
                break;
            case DiagnosticSeverity::Fatal: d.severity = Severity::Fatal; design.numUpstreamErrors++; break;
            default: break;
        }
        std::string mapped = opt.empty() ? "" : ruleForSlangOption(opt);
        if (!opt.empty())
            d.ruleId = mapped.empty() ? "SLANG-" + opt : mapped;
        else {
            d.ruleId = ruleForSlangDiag(rd.originalDiagnostic.code);
            if (d.ruleId.empty()) d.ruleId = "LNT-001";   // syntax / preprocess / other elaboration errors
        }
        // An operand-type mismatch is a width problem (LNT-004) only when the two readable integral types differ in width;
        // every other case (same width with another signedness or type, or types that cannot be read) is LNT-005.
        if (d.ruleId == "LNT-004" && (opt == "arith-op-mismatch" || opt == "bitwise-op-mismatch")) {
            const auto& args = rd.originalDiagnostic.args;
            auto typeOf = [](const slang::Diagnostic::Arg& a) -> const slang::ast::Type* {
                if (const auto* c = std::get_if<slang::Diagnostic::CustomArgType>(&a))
                    if (const auto* t = std::any_cast<const slang::ast::Type*>(&c->second)) return *t;
                return nullptr;
            };
            bool widthsDiffer = false, sameWidthReadable = false;
            if (args.size() >= 2) {
                const auto* l = typeOf(args[0]);
                const auto* r = typeOf(args[1]);
                widthsDiffer = l && r && l->isIntegral() && r->isIntegral() && l->getBitWidth() != r->getBitWidth();
                sameWidthReadable = l && r && l->isIntegral() && r->isIntegral() && !widthsDiffer;
            }
            if (!widthsDiffer) d.ruleId = "LNT-005";
            // A bitwise operation of two integral operands of one width computes on bit patterns, whatever their types, signedness
            // or 2/4-state kinds (an enum xor a vector): no operand is extended, truncated or reinterpreted by value.
            if (sameWidthReadable && opt == "bitwise-op-mismatch" && d.severity == Severity::Warning) {
                d.severity = Severity::Info;
                d.nextStep = "No action unless the type difference is a mistake: a bitwise operation of one width computes on the same bits.";
            }
        }
        // An unused package member cannot affect behavior; retain slang's original severity separately.
        if (d.ruleId == "LNT-017" && opt == "case-enum-explicit" && d.severity == Severity::Warning)
            d.severity = Severity::Info; // The syntactic default handles unnamed enumerators.
        if (d.severity == Severity::Warning &&
            (d.ruleId == "LNT-077" || d.ruleId == "LNT-011" || d.ruleId == "LNT-059" || d.ruleId == "LNT-078" ||
             (d.ruleId == "LNT-010" && opt == "unconnected-output-port"))) d.severity = Severity::Info;
        // Style and redundancy slang reports from its opt-in groups (-Wextra, -Wparentheses, macro hygiene): synthesis
        // builds what the code says, so they are advisory (docs/rules/LINT_WORKFLOW.md, severity policy). The upstream
        // severity stays in the report.
        static const std::set<std::string> styleOptions = {"case-redundant-default", "useless-cast", "ignored-macro-paste",
                                                           "bitwise-op-parentheses", "logical-op-parentheses"};
        if (d.severity == Severity::Warning && styleOptions.count(opt)) d.severity = Severity::Info;
        // $bits of an integer-typed constant is its type's width: slang's warning is that the author likely meant the constant's value
        // or $clog2. The hardware is what is written (a 32-bit constant), simulation and synthesis agree, so it is advisory.
        if (d.severity == Severity::Warning && opt == "bits-of-integer-constant") {
            d.severity = Severity::Info;
            d.nextStep = "Check that the width of the type, not the constant's value or its $clog2, is what was meant; "
                         "a comparison of two such widths is true for every value.";
        }
        // The same bits under another type (docs/rules/LNT.md, LNT-005): synthesis builds them as written, so these are advisory;
        // an enum destination, a width change or a 4-state value into a 2-state one keep slang's WARNING.
        if (d.severity == Severity::Warning && d.ruleId == "LNT-005" &&
            ((opt == "packed-array-conv" && reinterpretsBits(rd.originalDiagnostic)) ||
             (opt == "case-type" && enumAgainstPlainVector(rd.originalDiagnostic)))) {
            d.severity = Severity::Info;
            d.nextStep = "No action unless the type difference is a mistake: synthesis builds the same bits.";
        }
        // Use upstream code and argument values, never its message format string.
        d.identityKey = std::string(slang::toString(rd.originalDiagnostic.code));
        for (const auto& arg : rd.originalDiagnostic.args) {
            const auto value = engine->formatArg(arg);
            d.identityKey += ":" + std::to_string(arg.index());
            d.identityArguments.push_back(value);
        }
        d.message = rd.formattedMessage;
        d.location = toLoc(sm, rd.location);
        if (d.ruleId == "LNT-009") {
            const auto* value = rd.originalDiagnostic.symbol
                                    ? rd.originalDiagnostic.symbol->as_if<slang::ast::ValueSymbol>()
                                    : nullptr;
            if (value && (value->kind == slang::ast::SymbolKind::Variable ||
                          value->kind == slang::ast::SymbolKind::Net ||
                          value->kind == slang::ast::SymbolKind::Port))
                d.signals.emplace_back(std::string(value->name));
        }
        if (rd.originalDiagnostic.code == slang::diag::InferredLatch &&
            !rd.originalDiagnostic.args.empty()) {
            const auto* path = std::get_if<std::string>(&rd.originalDiagnostic.args.front());
            const auto* proc = rd.originalDiagnostic.symbol
                                   ? rd.originalDiagnostic.symbol->as_if<slang::ast::ProceduralBlockSymbol>()
                                   : nullptr;
            const auto* scope = proc ? proc->getParentScope() : nullptr;
            // ValuePath is an upstream structured argument. Accept it only when it exactly
            // resolves to a value symbol in the procedure's containing scope; don't parse it.
            const auto* target = path && scope ? scope->find(*path) : nullptr;
            if (target && (target->kind == slang::ast::SymbolKind::Variable ||
                           target->kind == slang::ast::SymbolKind::Port ||
                           target->kind == slang::ast::SymbolKind::Net)) {
                struct References : slang::ast::ASTVisitor<References, slang::ast::VisitFlags::AllGood> {
                    const slang::ast::Symbol* target;
                    bool seen = false, exact = true;
                    explicit References(const slang::ast::Symbol* symbol) : target(symbol) {}
                    void handle(const slang::ast::NamedValueExpression& e) {
                        if (e.symbol.name != target->name) return;
                        seen = true;
                        if (&e.symbol != target) exact = false;
                    }
                } refs(target);
                proc->getBody().visit(refs);
                if (refs.seen && refs.exact) d.signals.emplace_back(std::string(target->name));
            }
        }
        if (auto* sym = rd.originalDiagnostic.symbol) {
            const auto& cb = engine->getSymbolPathCB();
            if (cb) d.hierarchy = cb(*sym);
        }
        for (auto& loc : rd.expansionLocs) {
            Evidence e;
            e.kind = "macro_expansion";
            e.text = "expanded from macro";
            e.location = toLoc(sm, loc);
            d.evidence.push_back(std::move(e));
        }
        for (auto& n : rd.notes) {
            Evidence e;
            e.kind = "slang_note";
            e.text = n.formattedMessage;
            e.location = toLoc(sm, n.location);
            d.evidence.push_back(std::move(e));
        }
        if (rd.originalDiagnostic.coalesceCount)
            // slang reports identical findings of one module body once; only the first instance is named.
            d.evidence.push_back({"occurrence_list_incomplete",
                                  "slang coalesced this finding across identical instances (slang count " +
                                  std::to_string(*rd.originalDiagnostic.coalesceCount) +
                                  "); only the first instance is listed in 'hierarchy'", {}});
        d.confidence = Confidence::Established;
        d.nextStep = d.severity == Severity::Error || d.severity == Severity::Fatal
                         ? "Fix the source so it parses and elaborates cleanly; results are incomplete until then."
                         : "Review the upstream slang finding; fix it or waive it with a reason and owner.";
        out.add(std::move(d));
    }

    Report& out;
    Design& design;
public:
    bool sawTooManyErrors = false;
};

} // namespace

std::string slangVersionString() {
    return std::to_string(slang::VersionInfo::getMajor()) + "." + std::to_string(slang::VersionInfo::getMinor()) +
           "." + std::to_string(slang::VersionInfo::getPatch());
}

namespace {

/// Options that make slang drop or alter diagnostics. Anything here makes the upstream scope INCOMPLETE.
bool isSuppressingOption(const std::string& a) {
    static const char* prefixes[] = {"-Wno-", "-Wnone", "--suppress-warnings", "--suppress-macro-warnings",
                                     "--compat", "--lint-only", "--ignore-unknown-modules", "--waiver-file",
                                     "--allow-use-before-declare", "--error-limit", "--relax-", "--allow-",
                                     "--translate-off-format", "--ignore-directive"};
    for (auto* p : prefixes)
        if (a.rfind(p, 0) == 0) return true;
    return a == "-q" || a == "--quiet" || a == "-Wno-error" ;
}

/// Reads filelists (-f/-F, recursively, bounded) and collects the suppressing options inside them.
void scanFilelists(const std::vector<std::string>& args, std::vector<std::string>& found, int depth = 0) {
    if (depth > 8) return;
    for (size_t i = 0; i < args.size(); i++) {
        std::string path;
        if ((args[i] == "-f" || args[i] == "-F" || args[i] == "-C") && i + 1 < args.size()) path = args[i + 1];
        else if (args[i].rfind("-f", 0) == 0 && args[i].size() > 2 && args[i][2] != '-' && args[i][2] != '=') path = args[i].substr(2);
        if (path.empty()) continue;
        std::ifstream f(path);
        if (!f) continue;
        std::vector<std::string> toks;
        std::string line;
        while (std::getline(f, line)) {
            auto h = line.find("//");
            if (h != std::string::npos) line.resize(h);
            std::istringstream is(line);
            for (std::string t; is >> t;) toks.push_back(t);
        }
        for (auto& t : toks)
            if (isSuppressingOption(t)) found.push_back(t + " (in filelist " + path + ")");
        scanFilelists(toks, found, depth + 1);
    }
}

} // namespace

std::unique_ptr<Design> loadDesign(const FrontendOptions& opts, Report& report) {
    auto design = std::make_unique<Design>();
    design->driver = std::make_unique<slang::driver::Driver>();
    auto& drv = *design->driver;
    drv.addStandardArgs();
    report.declareScope("elaboration");
    report.declareScope("upstream-diagnostics");

    std::vector<std::string> args = {"sverity"};
    for (auto& a : opts.slangArgs) args.push_back(a);

    // Everything slang prints on stderr/stdout is captured so the reason for a load failure is not lost.
    auto fatal = [&](const std::string& msg) -> std::unique_ptr<Design> {
        Diagnostic d;
        d.ruleId = "LNT-001";
        d.severity = Severity::Fatal;
        d.message = msg;
        std::string captured = slang::OS::capturedStderr + slang::OS::capturedStdout;
        if (!captured.empty()) d.evidence.push_back({"slang_output", captured, {}});
        d.nextStep = "Fix the command line / file list.";
        report.add(std::move(d));
        report.markIncomplete("elaboration", msg);
        report.setExecutionError(msg);
        return nullptr;
    };

    // Suppression is decided from what slang will actually do, not only from argv.
    std::vector<std::string> suppressing;
    for (auto& a : opts.slangArgs)
        if (isSuppressingOption(a)) suppressing.push_back(a);
    scanFilelists(opts.slangArgs, suppressing);

    std::vector<const char*> argv;
    for (auto& s : args) argv.push_back(s.c_str());
    auto capture = slang::OS::captureOutput();
    if (!drv.parseCommandLine(static_cast<int>(argv.size()), argv.data()))
        return fatal("command line or file list could not be loaded");
    if (opts.enableAllUpstreamWarnings)
        drv.options.warningOptions.insert(drv.options.warningOptions.begin(), "everything");
    else
        report.markIncomplete("upstream-diagnostics", "slang's default warning set is used instead of -Weverything");
    for (auto& w : drv.options.warningOptions)
        if (w == "none" || w.rfind("no-", 0) == 0)
            suppressing.push_back("-W" + w);
    bool userErrorLimit = drv.options.errorLimit.has_value();
    if (!drv.processOptions()) return fatal("slang rejected the provided options or input files");

    for (auto& s : suppressing) {
        if (s.rfind("--error-limit", 0) == 0 && s.find("=0") != std::string::npos) continue;   // unlimited is not a gap
        report.markIncomplete("upstream-diagnostics", "slang option '" + s + "' may suppress or limit diagnostics");
    }
    // slang's own waiver files delete diagnostics before any client sees them: refuse to honour them.
    bool hadSlangWaivers = !drv.options.waiverFiles.empty() || drv.diagEngine.getWaiverManager() != nullptr;
    if (hadSlangWaivers) {
        drv.diagEngine.setWaiverManager(nullptr);
        Diagnostic d;
        d.ruleId = "CST-006";
        d.severity = Severity::Warning;
        d.message = "slang waiver files (--waiver-file) are ignored by SVerity because slang would delete the "
                    "waived diagnostics; use --waivers so waived findings stay in the report";
        d.confidence = Confidence::Unresolved;
        d.nextStep = "Convert the waivers to SVerity's format and pass them with --waivers.";
        report.add(std::move(d));
    }
    // Never let slang stop reporting after N errors: that would hide later findings silently.
    if (!userErrorLimit) drv.diagEngine.setErrorLimit(0);

    // Replace slang's stderr text client with the SVerity capture client.
    drv.diagEngine.clearClients();
    const bool compat = std::any_of(args.begin(), args.end(), [](const std::string& a) { return a.rfind("--compat", 0) == 0; });
    auto client = std::make_shared<CaptureClient>(report, *design, compat);
    drv.diagEngine.addClient(client);

    if (!drv.parseAllSources()) return fatal("source files could not be read or parsed");

    // In-source `pragma diagnostic ignore` silently hides findings: surface every one.
    // (copy first: visiting holds the SourceManager lock and location lookups may need to take it too)
    struct IgnoreDirective { slang::BufferID buf; size_t offset; std::string name; };
    std::vector<IgnoreDirective> ignoreDirectives;
    drv.sourceManager.visitDiagnosticDirectives([&](slang::BufferID buf, auto& directives) {
        for (auto& dd : directives)
            if (dd.severity == slang::DiagnosticSeverity::Ignored)
                ignoreDirectives.push_back({buf, dd.offset, std::string(dd.name)});
    });
    for (auto& dd : ignoreDirectives) {
        Diagnostic d;
        d.ruleId = "CST-010";
        d.severity = Severity::Warning;
        d.message = "source pragma suppresses slang diagnostic '" + dd.name +
                    "'; findings of that kind below this point may be hidden";
        d.location = toLoc(drv.sourceManager, slang::SourceLocation(dd.buf, dd.offset));
        d.confidence = Confidence::Unresolved;
        d.nextStep = "Remove the pragma or waive the finding explicitly with --waivers.";
        report.add(std::move(d));
        report.markIncomplete("upstream-diagnostics", "in-source diagnostic pragma suppresses '" + dd.name + "'");
    }

    design->compilation = drv.createCompilation();
    // getAllDiagnostics() already contains the parse diagnostics (do not issue them twice).
    drv.diagEngine.issue(design->compilation->getAllDiagnostics());
    // Analysis on a thread pool names whichever instance finishes first in a coalesced finding, so a report would vary between runs.
    drv.threadPool.reset();
    design->analysis = drv.runAnalysis(*design->compilation);

    if (client->sawTooManyErrors) {
        report.markIncomplete("upstream-diagnostics", "slang stopped after reaching its error limit (--error-limit)");
        design->numUpstreamErrors--;   // the 'too many errors' fatal is not a design error
    }
    if (design->numUpstreamErrors > 0) {
        Diagnostic d;
        d.ruleId = "LNT-001";
        d.severity = Severity::Fatal;
        d.message = std::to_string(design->numUpstreamErrors) +
                    " syntax/preprocess/elaboration error(s) reported by slang; analysis results derived from an "
                    "erroneous design cannot be treated as complete";
        d.nextStep = "Resolve the slang errors listed above, then re-run.";
        report.add(std::move(d));
        report.markIncomplete("elaboration", "slang reported elaboration errors");
    }
    for (auto buffer : drv.sourceManager.getAllBuffers()) {
        const auto kind = drv.sourceManager.getBufferKind(buffer);
        if (kind == slang::SourceManager::BufferKind::DesignFile ||
            kind == slang::SourceManager::BufferKind::IncludeFile ||
            kind == slang::SourceManager::BufferKind::LibraryFile)
            report.addIdentityFile(drv.sourceManager.getFullPath(buffer).generic_string());
    }
    return design;
}

} // namespace sverity
