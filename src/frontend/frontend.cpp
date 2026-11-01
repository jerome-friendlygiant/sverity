#include "sverity/frontend.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "slang/analysis/AnalysisManager.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
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

/// Receives every diagnostic slang issues and converts it to a SVerity Diagnostic. Nothing is
/// filtered here: ignored/notes never reach clients by slang's own design, everything else does.
class CaptureClient : public slang::DiagnosticClient {
public:
    CaptureClient(Report& r, Design& d) : out(r), design(d) {}

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
        switch (rd.severity) {
            case DiagnosticSeverity::Note: d.severity = Severity::Info; break;
            case DiagnosticSeverity::Warning: d.severity = Severity::Warning; design.numUpstreamWarnings++; break;
            case DiagnosticSeverity::Error: d.severity = Severity::Error; design.numUpstreamErrors++; break;
            case DiagnosticSeverity::Fatal: d.severity = Severity::Fatal; design.numUpstreamErrors++; break;
            default: break;
        }
        std::string mapped = opt.empty() ? "" : ruleForSlangOption(opt);
        if (!opt.empty())
            d.ruleId = mapped.empty() ? "SLANG-" + opt : mapped;
        else
            d.ruleId = "LNT-001";   // syntax / preprocess / elaboration errors have no option name
        d.message = rd.formattedMessage;
        d.location = toLoc(sm, rd.location);
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
                                     "--allow-use-before-declare", "--error-limit"};
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
    auto client = std::make_shared<CaptureClient>(report, *design);
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
    return design;
}

} // namespace sverity
