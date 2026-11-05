#include "sverity/report.h"
#include <fstream>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <set>
#include <tuple>

#include "sverity/catalog.h"

namespace sverity {

const char* toString(ExecutionStatus s) { return s == ExecutionStatus::Success ? "SUCCESS" : "ERROR"; }
const char* toString(AnalysisCoverage c) { return c == AnalysisCoverage::Complete ? "COMPLETE" : "INCOMPLETE"; }
const char* toString(ReviewStatus r) {
    switch (r) {
        case ReviewStatus::Pass: return "PASS";
        case ReviewStatus::ReviewRequired: return "REVIEW_REQUIRED";
        case ReviewStatus::Fail: return "FAIL";
    }
    return "?";
}
const char* toString(RuleState s) {
    switch (s) {
        case RuleState::Executed: return "EXECUTED";
        case RuleState::Partial: return "PARTIAL";
        case RuleState::NotRun: return "NOT_RUN";
        case RuleState::Disabled: return "DISABLED";
        case RuleState::Unimplemented: return "UNIMPLEMENTED";
        case RuleState::Failed: return "FAILED";
    }
    return "?";
}

void Report::add(Diagnostic d) {
    // Severity contract (RFC section 3): an unproved property is at least a WARNING. INFO with proof state NOT_ATTEMPTED is allowed only for a
    // recognised structure that states its trust model (evidence "trust_model"); a bare obligation is raised to WARNING here, in one place.
    if (d.severity == Severity::Info && d.proofState == ProofState::NotAttempted) {
        bool trusted = std::any_of(d.evidence.begin(), d.evidence.end(), [](const Evidence& e) { return e.kind == "trust_model"; });
        if (!trusted) d.severity = Severity::Warning;
    }
    if (d.disposition == Disposition::Open) d.applyDefaultDisposition();
    diags_.push_back(std::move(d));
    finalized_ = false;
}

void Report::declareScope(const std::string& scope) {
    for (auto& s : scopes_)
        if (s.scope == scope) return;
    scopes_.push_back({scope, AnalysisCoverage::Complete, {}});
}

void Report::markIncomplete(const std::string& scope, const std::string& reason) {
    declareScope(scope);
    for (auto& s : scopes_) {
        if (s.scope != scope) continue;
        s.coverage = AnalysisCoverage::Incomplete;
        if (std::find(s.reasons.begin(), s.reasons.end(), reason) == s.reasons.end())
            s.reasons.push_back(reason);
    }
}

void Report::setExecutionError(const std::string& why) {
    if (executionError_.empty()) executionError_ = why;
}

void Report::setToolInfo(std::string version, std::string slangVersion, std::string catalogVersion) {
    toolVersion_ = std::move(version);
    slangVersion_ = std::move(slangVersion);
    catalogVersion_ = std::move(catalogVersion);
}

std::string Report::relativize(const std::string& path) const {
    if (pathRoot_.empty() || path.empty()) return path;
    std::string root = pathRoot_;
    if (root.back() != '/') root += '/';
    if (path.compare(0, root.size(), root) == 0) return path.substr(root.size());
    if (std::filesystem::path(path).is_absolute()) {
        auto relative = std::filesystem::path(path).lexically_normal().lexically_relative(pathRoot_);
        if (!relative.empty()) return relative.generic_string();
    }
    return path;
}

// Paths embedded by frontend diagnostics must obey the same report-root
// policy as structured source locations (e.g. an unreadable file name).
static void relativizeEmbeddedPaths(std::string& text, const std::string& root) {
    if (root.empty()) return;
    auto boundary = [](char c) { return std::isspace(static_cast<unsigned char>(c)) || c == '\'' || c == '"' || c == '(' || c == '='; };
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '/' || (i && !boundary(text[i - 1])) || i + 1 == text.size() || boundary(text[i + 1])) continue;
        size_t end = i + 1;
        char quote = i && (text[i - 1] == '\'' || text[i - 1] == '"') ? text[i - 1] : '\0';
        while (end < text.size() && (quote ? text[end] != quote :
               !boundary(text[end]) && text[end] != ')' && text[end] != ':' && text[end] != ',')) ++end;
        auto relative = std::filesystem::path(text.substr(i, end - i)).lexically_normal().lexically_relative(root);
        if (!relative.empty()) { auto path = relative.generic_string(); text.replace(i, end - i, path); i += path.size() - 1; }
    }
}

// One conservative grouping policy. LNT-025 uses an explicitly recorded
// source library, design unit, procedure ordinal and target. Everything else
// stays at its diagnostic identity.
// Absolute checkout paths, severity, disposition and visit order are excluded.
static std::string rootCauseOf(const Diagnostic& d, const std::map<std::string, std::string>& fileIds) {
    std::string key;
    auto part = [&](const std::string& value) { key += std::to_string(value.size()) + ":" + value; };
    part(d.ruleId);
    const std::string& hierarchy = d.occurrences.empty() ? d.hierarchy : d.occurrences.front();
    if (d.ruleId == "LNT-025" && !d.groupingUnit.empty() && d.groupingProcedureOrdinal && d.signals.size() == 1) {
        part("procedure-target"); part(d.groupingUnit); part(std::to_string(d.groupingProcedureOrdinal)); part(d.signals[0]);
    } else {
        part("finding");
        auto fileId = [&](const std::filesystem::path& path) {
            auto normalized = path.lexically_normal().generic_string();
            auto it = fileIds.find(normalized);
            return it == fileIds.end() ? normalized : it->second;
        };
        Diagnostic identity = d;
        identity.location.file = fileId(d.originalFile);
        for (size_t i = 0; i < identity.identityArguments.size(); ++i)
            if (std::filesystem::path(d.originalIdentityArguments[i]).is_absolute())
                identity.identityArguments[i] = fileId(d.originalIdentityArguments[i]);
        for (size_t i = 0; i < identity.evidence.size(); ++i)
            identity.evidence[i].location.file = fileId(i < d.originalEvidenceFiles.size() ?
                d.originalEvidenceFiles[i] : identity.evidence[i].location.file);
        part(findingIdentity(identity));
        part(hierarchy);
    }
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : key) { hash ^= ch; hash *= 1099511628211ull; }
    char buffer[32]; std::snprintf(buffer, sizeof buffer, "rc-%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

static int sevRank(Severity s) { return static_cast<int>(s); }

void Report::finalize() {
    for (auto& d : diags_)
        if (d.ruleId == "CDC-003" && d.disposition == Disposition::Waived && d.waiver)
            d.nextStep = "Retain and review the waiver justification and its scope; a waiver does not establish synchronisation or timing safety.";
    // Source lines come from the files on disk, before paths are normalised. Only findings with crossing facts get them.
    std::map<std::string, std::vector<std::string>> sourceFiles;
    const auto sourceLineAt = [&](const SourceLocation& loc) -> std::string {
        if (!loc.valid()) return {};
        auto [it, fresh] = sourceFiles.try_emplace(loc.file);
        if (fresh) {
            std::ifstream in(loc.file);
            for (std::string line; std::getline(in, line);) it->second.push_back(line);
        }
        if (loc.line > it->second.size()) return {};
        std::string line = it->second[loc.line - 1];
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) return {};
        line.erase(0, first);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        constexpr size_t kMaxShown = 160;
        if (line.size() > kMaxShown) line = line.substr(0, kMaxShown) + "...";
        return line;
    };
    for (auto& d : diags_) {
        // Once only: on a later finalize the paths are already normalised, and could open an unrelated file.
        if (!d.crossing || d.groupingOriginCaptured) continue;
        d.sourceLine = sourceLineAt(d.location);
        for (auto& e : d.evidence) e.sourceLine = sourceLineAt(e.location);
    }
    // 1. Normalize paths (reports must not leak or depend on the build machine's absolute paths).
    for (auto& d : diags_) {
        if (!d.groupingOriginCaptured) {
            d.originalFile = d.location.file; d.originalMessage = d.message;
            d.originalIdentityArguments = d.identityArguments;
            for (const auto& e : d.evidence) { d.originalEvidence.push_back(e.text); d.originalEvidenceFiles.push_back(e.location.file); }
            d.groupingOriginCaptured = true;
        }
        d.location.file = relativize(d.location.file);
        if (d.crossing) {
            d.crossing->launch.location.file = relativize(d.crossing->launch.location.file);
            d.crossing->capture.location.file = relativize(d.crossing->capture.location.file);
        }
        // $error, $warning and the like carry the design's own text, which is not a path to rewrite.
        if (d.message.rfind('$', 0) != 0) relativizeEmbeddedPaths(d.message, pathRoot_);
        for (auto& arg : d.identityArguments) relativizeEmbeddedPaths(arg, pathRoot_);
        relativizeEmbeddedPaths(d.nextStep, pathRoot_);
        for (auto& e : d.evidence) {
            e.location.file = relativize(e.location.file);
            relativizeEmbeddedPaths(e.text, pathRoot_);
        }
    }
    relativizeEmbeddedPaths(executionError_, pathRoot_);
    for (auto& scope : scopes_) for (auto& reason : scope.reasons) relativizeEmbeddedPaths(reason, pathRoot_);
    // 2. Group *exact* duplicates (same rule/severity/location/message/origin/upstream code),
    //    preserving every occurrence's hierarchy and the count. Nothing is dropped.
    std::vector<Diagnostic> grouped;
    std::map<std::string, size_t> seen;
    for (auto& d : diags_) {
        if (d.occurrences.empty() && !d.hierarchy.empty()) d.occurrences.push_back(d.hierarchy);
        // Only *exact* duplicates are grouped: every field except hierarchy/occurrences must be equal.
        std::string key = std::string(toString(d.severity)) + "\x1f" + d.ruleId + "\x1f" + d.location.file + "\x1f" +
                          std::to_string(d.location.line) + "\x1f" + std::to_string(d.location.column) + "\x1f" +
                          d.message + "\x1f" +
                          d.origin + "\x1f" + d.upstreamCode + "\x1f" + toString(d.disposition) + "\x1f" +
                          (d.waiver ? d.waiver->waiverId : "") + "\x1f" + toString(d.confidence) + "\x1f" +
                          toString(d.proofState) + "\x1f" + d.nextStep + "\x1f" + d.reviewerDecision + "\x1f" +
                          d.reviewerComment + "\x1f" + d.identityKey + "\x1f" + d.groupingUnit + "\x1f" +
                          std::to_string(d.groupingProcedureOrdinal);
        for (const auto& arg : d.identityArguments) key += "\x1c" + arg;
        for (auto& e : d.evidence)
            key += "\x1e" + e.kind + "\x1f" + e.text + "\x1f" + e.location.file + ":" + std::to_string(e.location.line) + ":" + std::to_string(e.location.column);
        for (auto& sgn : d.signals) key += "\x1d" + sgn;
        auto it = seen.find(key);
        if (it == seen.end()) {
            seen[key] = grouped.size();
            grouped.push_back(std::move(d));
        } else {
            auto& g = grouped[it->second];
            g.count += d.count;
            for (auto& o : d.occurrences)
                if (!o.empty() && std::find(g.occurrences.begin(), g.occurrences.end(), o) == g.occurrences.end())
                    g.occurrences.push_back(std::move(o));
        }
    }
    diags_ = std::move(grouped);
    // 3. Deterministic order.
    std::stable_sort(diags_.begin(), diags_.end(), [](const Diagnostic& a, const Diagnostic& b) {
        return std::tuple(a.location.file, a.location.line, a.location.column, a.ruleId, -sevRank(a.severity),
                          a.message) <
               std::tuple(b.location.file, b.location.line, b.location.column, b.ruleId, -sevRank(b.severity),
                          b.message);
    });
    // Give each source the shortest distinguishing suffix among the files
    // observed in this report. This does not use --root or a checkout prefix.
    std::set<std::string> paths;
    auto includePath = [&](const std::filesystem::path& path) {
        if (path.is_absolute()) paths.insert(path.lexically_normal().generic_string());
    };
    for (const auto& file : identityFiles_) includePath(file);
    for (const auto& d : diags_) {
        includePath(d.originalFile);
        for (const auto& arg : d.originalIdentityArguments) includePath(arg);
        for (const auto& file : d.originalEvidenceFiles) includePath(file);
    }
    std::map<std::string, size_t> suffixCounts;
    for (const auto& original : paths) {
        std::vector<std::string> components; std::filesystem::path suffix;
        for (const auto& component : std::filesystem::path(original).relative_path()) components.push_back(component.generic_string());
        for (auto it = components.rbegin(); it != components.rend(); ++it) {
            suffix = suffix.empty() ? std::filesystem::path(*it) : std::filesystem::path(*it) / suffix;
            ++suffixCounts[suffix.generic_string()];
        }
    }
    std::map<std::string, std::string> fileIds;
    for (const auto& original : paths) {
        std::filesystem::path path(original), suffix;
        std::vector<std::string> components;
        for (const auto& component : path.relative_path()) components.push_back(component.generic_string());
        for (auto it = components.rbegin(); it != components.rend(); ++it) {
            suffix = suffix.empty() ? std::filesystem::path(*it) : std::filesystem::path(*it) / suffix;
            if (suffixCounts[suffix.generic_string()] == 1) break;
        }
        fileIds[original] = suffix.generic_string();
    }
    auto identityOf = [&](const Diagnostic& d) {
        Diagnostic identity = d;
        auto canonicalFile = [&](const std::string& original, const std::string& relative) {
            auto it = fileIds.find(std::filesystem::path(original).lexically_normal().generic_string());
            return it == fileIds.end() ? relative : it->second;
        };
        identity.location.file = canonicalFile(d.originalFile, d.location.file);
        for (size_t i = 0; i < identity.identityArguments.size(); ++i)
            identity.identityArguments[i] = canonicalFile(d.originalIdentityArguments[i], d.identityArguments[i]);
        for (size_t i = 0; i < identity.evidence.size(); ++i)
            identity.evidence[i].location.file = canonicalFile(d.originalEvidenceFiles[i], d.evidence[i].location.file);
        return identity;
    };
    for (auto& d : diags_) {
        std::sort(d.occurrences.begin(), d.occurrences.end());
        d.fingerprint = computeFingerprint(identityOf(d));
        d.rootCause = rootCauseOf(d, fileIds);
    }
    // Findings that survived exact-duplicate grouping are different findings, so one fingerprint-only
    // waiver must not cover both (for example one line under two parameter values). Separate them by
    // instance, then by text. Only findings that would otherwise collide depend on these.
    auto separate = [&](bool useMessage) {
        std::map<std::string, std::vector<Diagnostic*>> byFingerprint;
        for (auto& d : diags_) byFingerprint[d.fingerprint].push_back(&d);
        for (auto& [fingerprint, group] : byFingerprint) {
            if (group.size() < 2) continue;
            for (Diagnostic* d : group) {
                Diagnostic identity = identityOf(*d);
                for (const auto& occurrence : d->occurrences) identity.identityKey += "\x1f" + occurrence;
                if (useMessage) {
                    identity.identityKey += "\x1e" + d->message;
                    for (const auto& e : d->evidence) identity.identityKey += "\x1d" + e.text;
                }
                d->fingerprint = computeFingerprint(identity);
            }
        }
    };
    separate(false);
    separate(true);
    // 4. Per-rule finding counts.
    std::map<std::string, uint32_t> perRule;
    for (auto& d : diags_) perRule[d.ruleId] += d.count;
    for (auto& rc : ruleCoverage_) rc.findings = perRule.count(rc.ruleId) ? perRule[rc.ruleId] : 0;
    std::sort(ruleCoverage_.begin(), ruleCoverage_.end(),
              [](const RuleCoverage& a, const RuleCoverage& b) { return a.ruleId < b.ruleId; });
    std::sort(scopes_.begin(), scopes_.end(),
              [](const ScopeCoverage& a, const ScopeCoverage& b) { return a.scope < b.scope; });
    finalized_ = true;
}

ExecutionStatus Report::executionStatus() const {
    if (!executionError_.empty()) return ExecutionStatus::Error;
    for (auto& d : diags_)
        if (d.severity == Severity::Fatal) return ExecutionStatus::Error;
    for (auto& rc : ruleCoverage_)
        if (rc.state == RuleState::Failed) return ExecutionStatus::Error;
    return ExecutionStatus::Success;
}

AnalysisCoverage Report::analysisCoverage() const {
    for (auto& s : scopes_)
        if (s.coverage == AnalysisCoverage::Incomplete) return AnalysisCoverage::Incomplete;
    // A catalog rule that is not executed means an advertised check was not evaluated.
    for (auto& rc : ruleCoverage_)
        if (rc.state != RuleState::Executed && !rc.preview) return AnalysisCoverage::Incomplete;
    if (executionStatus() == ExecutionStatus::Error) return AnalysisCoverage::Incomplete;
    return AnalysisCoverage::Complete;
}

ReviewStatus Report::reviewStatus() const {
    bool review = false;
    for (auto& d : diags_) {
        if (d.disposition == Disposition::Waived || d.disposition == Disposition::Resolved) continue;
        if (d.severity >= Severity::Error) return ReviewStatus::Fail;
        if (d.severity == Severity::Warning || d.disposition == Disposition::ReviewRequired) review = true;
    }
    return review ? ReviewStatus::ReviewRequired : ReviewStatus::Pass;
}

uint32_t Report::countSeverity(Severity s) const {
    uint32_t n = 0;
    for (auto& d : diags_) if (d.severity == s) n += d.count;
    return n;
}
uint32_t Report::countDisposition(Disposition x) const {
    uint32_t n = 0;
    for (auto& d : diags_) if (d.disposition == x) n += d.count;
    return n;
}

int Report::exitCode(bool strict) const {
    int code = 0;
    if (reviewStatus() == ReviewStatus::Fail) code |= 1;
    if (reviewStatus() == ReviewStatus::ReviewRequired) code |= 2;
    if (analysisCoverage() == AnalysisCoverage::Incomplete) code |= 4;
    if (executionStatus() == ExecutionStatus::Error) code |= 8;
    if (!strict) code &= (1 | 8);
    return code;
}

// ------------------------------------------------------------------- JSON
static json::Value locJson(const SourceLocation& l) {
    json::Value v = json::Value::object();
    if (!l.valid()) return v;
    v.set("file", l.file).set("line", l.line).set("column", l.column);
    return v;
}

static json::Value crossingJson(const Crossing& c) {
    auto endpoint = [](const CrossingEndpoint& e) {
        auto value = json::Value::object();
        value.set("signal", e.signal).set("clock_domain", e.clockDomain).set("sdc_clock", e.sdcClock)
            .set("edge", e.edge).set("location", locJson(e.location));
        return value;
    };
    auto value = json::Value::object();
    value.set("launch", endpoint(c.launch)).set("capture", endpoint(c.capture))
        .set("relationship", c.relationship).set("width_bits", c.widthBits).set("logic_on_path", c.logicOnPath);
    return value;
}

json::Value Report::toJson() const {
    json::Value root = json::Value::object();
    root.set("schema", "sverity-report/1");
    json::Value tool = json::Value::object();
    tool.set("name", "sverity").set("version", toolVersion_).set("slang_version", slangVersion_)
        .set("catalog_version", catalogVersion_);
    root.set("tool", tool);

    json::Value sum = json::Value::object();
    sum.set("execution_status", toString(executionStatus()));
    sum.set("analysis_coverage", toString(analysisCoverage()));
    sum.set("review_status", toString(reviewStatus()));
    if (!executionError_.empty()) sum.set("execution_error", executionError_);
    json::Value sev = json::Value::object();
    for (auto s : {Severity::Fatal, Severity::Error, Severity::Warning, Severity::Info})
        sev.set(toString(s), countSeverity(s));
    sum.set("severity_counts", sev);
    json::Value disp = json::Value::object();
    for (auto d : {Disposition::Open, Disposition::ReviewRequired, Disposition::Resolved, Disposition::Waived})
        disp.set(toString(d), countDisposition(d));
    sum.set("disposition_counts", disp);
    uint32_t cnt[6] = {0, 0, 0, 0, 0, 0};
    for (auto& rc : ruleCoverage_) cnt[static_cast<int>(rc.state)]++;
    json::Value rules = json::Value::object();
    rules.set("total", static_cast<uint32_t>(ruleCoverage_.size()))
        .set("executed", cnt[static_cast<int>(RuleState::Executed)])
        .set("partial", cnt[static_cast<int>(RuleState::Partial)])
        .set("disabled", cnt[static_cast<int>(RuleState::Disabled)])
        .set("unimplemented", cnt[static_cast<int>(RuleState::Unimplemented)])
        .set("not_run", cnt[static_cast<int>(RuleState::NotRun)])
        .set("failed", cnt[static_cast<int>(RuleState::Failed)]);
    sum.set("rules", rules);
    sum.set("exit_code_strict", exitCode(true));
    root.set("summary", sum);
    root.set("inputs", inputs_);

    json::Value cov = json::Value::object();
    json::Value rcs = json::Value::array();
    for (auto& rc : ruleCoverage_) {
        json::Value r = json::Value::object();
        if (rc.preview) r.set("preview", true);
        r.set("rule_id", rc.ruleId).set("state", toString(rc.state)).set("catalog_status", rc.catalogStatus)
            .set("objects_evaluated", rc.objectsEvaluated).set("findings", rc.findings);
        json::Value severities = json::Value::object(), dispositions = json::Value::object();
        for (auto severity : {Severity::Fatal, Severity::Error, Severity::Warning, Severity::Info}) {
            uint32_t n = 0; for (const auto& d : diags_) if (d.ruleId == rc.ruleId && d.severity == severity) n += d.count;
            severities.set(toString(severity), n);
        }
        for (auto disposition : {Disposition::Open, Disposition::ReviewRequired, Disposition::Resolved, Disposition::Waived}) {
            uint32_t n = 0; for (const auto& d : diags_) if (d.ruleId == rc.ruleId && d.disposition == disposition) n += d.count;
            dispositions.set(toString(disposition), n);
        }
        r.set("severity_counts", severities).set("disposition_counts", dispositions);
        if (!rc.reason.empty()) r.set("reason", rc.reason);
        rcs.push(r);
    }
    cov.set("rules", rcs);
    json::Value scs = json::Value::array();
    for (auto& s : scopes_) {
        json::Value o = json::Value::object();
        o.set("scope", s.scope).set("analysis_coverage", toString(s.coverage));
        json::Value rs = json::Value::array();
        for (auto& r : s.reasons) rs.push(r);
        o.set("reasons", rs);
        scs.push(o);
    }
    cov.set("scopes", scs);
    json::Value objs = json::Value::object();
    for (auto& [k, v] : objectCounts_) objs.set(k, v);
    cov.set("design_objects", objs);
    root.set("coverage", cov);

    json::Value ws = json::Value::array();
    for (auto& w : waivers_) {
        json::Value o = json::Value::object();
        o.set("id", w.id).set("rule", w.rule).set("state", w.state).set("matched", w.matched)
            .set("reason", w.reason).set("owner", w.owner).set("expires", w.expires).set("scope", w.scope);
        ws.push(o);
    }
    root.set("waivers", ws);

    json::Value ds = json::Value::array();
    for (auto& d : diags_) {
        json::Value o = json::Value::object();
        o.set("fingerprint", d.fingerprint).set("rule_id", d.ruleId).set("severity", toString(d.severity))
            .set("root_cause", d.rootCause).set("message", d.message).set("location", locJson(d.location)).set("hierarchy", d.hierarchy)
            .set("confidence", toString(d.confidence)).set("proof_state", toString(d.proofState))
            .set("disposition", toString(d.disposition)).set("origin", d.origin);
        if (d.originalSeverity) o.set("original_severity", toString(*d.originalSeverity));
        if (d.requestedSeverity) o.set("requested_severity", toString(*d.requestedSeverity));
        if (!d.severityOverrideReason.empty()) o.set("severity_override_reason", d.severityOverrideReason);
        if (!d.upstreamCode.empty()) o.set("upstream_code", d.upstreamCode);
        if (!d.upstreamSeverity.empty()) o.set("upstream_severity", d.upstreamSeverity);
        json::Value ev = json::Value::array();
        for (auto& e : d.evidence) {
            json::Value x = json::Value::object();
            x.set("kind", e.kind).set("text", e.text).set("location", locJson(e.location));
            if (!e.sourceLine.empty()) x.set("source_line", e.sourceLine);
            ev.push(x);
        }
        o.set("evidence", ev);
        if (d.crossing) o.set("crossing", crossingJson(*d.crossing));
        if (!d.sourceLine.empty()) o.set("source_line", d.sourceLine);
        json::Value sg = json::Value::array();
        for (auto& s : d.signals) sg.push(s);
        o.set("signals", sg);
        o.set("next_step", d.nextStep);
        o.set("count", d.count);
        json::Value oc = json::Value::array();
        for (auto& h : d.occurrences) oc.push(h);
        o.set("occurrences", oc);
        if (!d.reviewerDecision.empty()) o.set("reviewer_decision", d.reviewerDecision);
        if (!d.reviewerComment.empty()) o.set("reviewer_comment", d.reviewerComment);
        if (d.waiver) {
            json::Value w = json::Value::object();
            w.set("id", d.waiver->waiverId).set("reason", d.waiver->reason).set("owner", d.waiver->owner)
                .set("expires", d.waiver->expires).set("scope", d.waiver->scope)
                .set("source_location", locJson(d.waiver->sourceLocation));
            o.set("waiver", w);
        }
        ds.push(o);
    }
    root.set("diagnostics", ds);
    return root;
}

// The canonical serializer owns finding fields and accounting; events never duplicate it.
std::string Report::toJsonl(bool strict, int extra) const {
    const auto data = toJson();
    std::string out;
    uint64_t sequence = 0;
    auto emit = [&](const char* type, json::Value record) {
        record.set("schema", "sverity-events/1").set("schema_version", "1.0.0")
            .set("sequence", ++sequence).set("type", type);
        out += record.dump(-1) + "\n";
    };
    emit("run_start", json::Value::object().set("tool", data["tool"]).set("inputs", data["inputs"]));
    for (const auto& d : data["diagnostics"].asArray())
        emit("finding", json::Value::object().set("fingerprint", d["fingerprint"]).set("diagnostic", d));
    emit("coverage", json::Value::object().set("coverage", data["coverage"]));
    for (const auto& w : data["waivers"].asArray()) emit("waiver", json::Value::object().set("waiver", w));
    auto summary = data["summary"];
    if (extra & 8) summary.set("execution_status", "ERROR");
    uint64_t occurrences = 0;
    for (const auto& d : diags_) occurrences += d.count;
    emit("run_end", json::Value::object().set("summary", summary)
        .set("exit_policy", strict ? "strict" : "violations").set("exit_code", exitCode(strict) | extra)
        .set("finding_records", static_cast<uint64_t>(diags_.size())).set("occurrences", occurrences)
        .set("record_count", sequence + 1).set("complete", true).set("truncated", false));
    return out;
}

static std::string htmlEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c;
        }
    }
    return out;
}

// Render arbitrary future fields without losing their contents.
static std::string htmlValue(const json::Value& v) {
    if (v.isObject()) {
        std::string s = "<ul>";
        for (const auto& f : v.asObject()) s += "<li><strong>" + htmlEscape(f.first) + ":</strong> " + htmlValue(f.second) + "</li>";
        return s + "</ul>";
    }
    if (v.isArray()) {
        std::string s = "<ul>";
        for (const auto& x : v.asArray()) s += "<li>" + htmlValue(x) + "</li>";
        return s + "</ul>";
    }
    return htmlEscape(v.isString() ? v.asString() : v.dump(-1));
}

std::string Report::toHtml() const {
    const auto data = toJson();
    auto show = [](const json::Value& v) { return htmlEscape(v.isString() ? v.asString() : v.dump(-1)); };
    auto extras = [](const json::Value& v, std::initializer_list<const char*> known) {
        std::string s;
        for (const auto& f : v.asObject()) {
            bool found = false;
            for (auto k : known) if (f.first == k) found = true;
            if (!found) s += "<li><strong>" + htmlEscape(f.first) + ":</strong> " + htmlValue(f.second) + "</li>";
        }
        return s.empty() ? s : "<ul>" + s + "</ul>";
    };
    auto location = [&](const json::Value& v) {
        if (v["file"].isNull()) return std::string("location unavailable") + extras(v, {"file", "line", "column"});
        return show(v["file"]) + ":" + show(v["line"]) + ":" + show(v["column"]) + extras(v, {"file", "line", "column"});
    };
    std::string out = R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>SVerity report</title><style>
:root{color-scheme:light dark}body{font:16px/1.5 system-ui,sans-serif;max-width:100rem;margin:auto;padding:1.5rem;overflow-wrap:anywhere}table{border-collapse:collapse;width:100%;font-size:.85em}th,td{border:1px solid #888;padding:.3rem;text-align:left;vertical-align:top}article{border-top:1px solid #888;padding:.5rem 0}summary{cursor:pointer;font-weight:bold}ul{padding-left:1.5em}.dimensions{display:flex;flex-wrap:wrap;gap:1em}.dimensions p{border:1px solid #888;padding:.8em;flex:1;min-width:14em}.quiet{opacity:.55}.state{font-weight:bold}@media(max-width:700px){table,tbody,tr,td,th{display:block}thead{display:none}td:before{content:attr(data-label) ': ';font-weight:bold}}@media print{body{font-size:10pt}article{break-inside:avoid}details:not([open])>*{display:block}}
</style></head><body><h1>SVerity report</h1><div class="dimensions">)";
    const auto& sum = data["summary"];
    for (const auto& item : {std::pair<const char*,const char*>{"execution_status", "Whether the tool completed its run."}, {"analysis_coverage", "Whether the declared analysis scopes were complete."}, {"review_status", "Whether findings require action or review."}})
        out += "<p><strong>" + std::string(item.first) + ": " + show(sum[item.first]) + "</strong><br>" + item.second + "</p>";
    out += "</div><h2>What this run did not establish</h2><p>Unimplemented: " + show(sum["rules"]["unimplemented"]) + "; partial: " + show(sum["rules"]["partial"]) + "; disabled: " + show(sum["rules"]["disabled"]) + ".</p><ul>";
    for (const auto& d : data["diagnostics"].asArray()) if (d["rule_id"].asString() == "CST-019") out += "<li>CST-019: " + show(d["message"]) + "</li>";
    for (const auto& scope : data["coverage"]["scopes"].asArray()) if (scope["analysis_coverage"].asString() == "INCOMPLETE") out += "<li>" + show(scope["scope"]) + ": INCOMPLETE " + htmlValue(scope["reasons"]) + "</li>";
    out += "</ul><h2>Rule accounting</h2>" + htmlValue(sum["rules"]) + "<h2>Rule coverage</h2><details><summary>Rule table</summary><table><thead><tr>";
    const std::vector<std::string> cols = {"rule_id","state","catalog_status","objects_evaluated","findings","FATAL","ERROR","WARNING","INFO","OPEN","REVIEW_REQUIRED","RESOLVED","WAIVED","reason"};
    for (const auto& c : cols) out += "<th>" + htmlEscape(c) + "</th>";
    out += "</tr></thead><tbody>";
    std::vector<const json::Value*> rules;
    for (const auto& r : data["coverage"]["rules"].asArray()) rules.push_back(&r);
    std::stable_sort(rules.begin(), rules.end(), [](const auto* a, const auto* b) {
        for (const char* k : {"FATAL", "ERROR", "WARNING", "INFO"}) {
            auto x = (*a)["severity_counts"][k].asInt(), y = (*b)["severity_counts"][k].asInt();
            if (x != y) return x > y;
        }
        return (*a)["rule_id"].asString() < (*b)["rule_id"].asString();
    });
    for (const auto* r : rules) {
        out += "<tr>";
        for (const auto& c : cols) {
            const auto& v = (c == "FATAL" || c == "ERROR" || c == "WARNING" || c == "INFO") ? (*r)["severity_counts"][c] : (c == "OPEN" || c == "REVIEW_REQUIRED" || c == "RESOLVED" || c == "WAIVED") ? (*r)["disposition_counts"][c] : (*r)[c];
            out += "<td data-label=\"" + htmlEscape(c) + "\"" + (v.dump(-1) == "0" ? " class=\"quiet\"" : c == "state" && v.asString() != "EXECUTED" ? " class=\"state\"" : "") + ">" + (v.isNull() ? "" : show(v)) + "</td>";
        }
        out += "</tr><tr><td colspan=\"14\">" + extras(*r, {"rule_id","state","catalog_status","objects_evaluated","findings","severity_counts","disposition_counts","reason"}) + extras((*r)["severity_counts"], {"FATAL","ERROR","WARNING","INFO"}) + extras((*r)["disposition_counts"], {"OPEN","REVIEW_REQUIRED","RESOLVED","WAIVED"}) + "</td></tr>";
    }
    out += "</tbody></table></details><h2>Findings</h2>";
    std::map<std::string, std::vector<const json::Value*>> groups;
    for (const auto& d : data["diagnostics"].asArray()) groups[d["rule_id"].asString()].push_back(&d);
    auto rank = [](const std::string& s) { return s == "FATAL" ? 0 : s == "ERROR" ? 1 : s == "WARNING" ? 2 : 3; };
    std::vector<std::string> ids;
    for (const auto& g : groups) ids.push_back(g.first);
    auto worst = [&](const std::string& id) { int n = 3; for (auto d : groups[id]) n = std::min(n, rank((*d)["severity"].asString())); return n; };
    std::stable_sort(ids.begin(), ids.end(), [&](const auto& a, const auto& b) { return worst(a) != worst(b) ? worst(a) < worst(b) : a < b; });
    for (const auto& id : ids) {
        const auto* info = Catalog::embedded().find(id);
        std::string counts;
        for (const char* severity : {"FATAL", "ERROR", "WARNING", "INFO"}) {
            unsigned n = 0;
            for (const auto* d : groups[id]) if ((*d)["severity"].asString() == severity) n += (*d)["count"].asInt();
            if (n) counts += "; " + std::string(severity) + " " + std::to_string(n);
        }
        out += "<details" + std::string(worst(id) <= 1 ? " open" : "") + "><summary>" + htmlEscape(id) + " — " + htmlEscape(info ? info->title : "Unknown rule") + " (" + std::to_string(groups[id].size()) + " findings" + counts + ")</summary>";
        for (const auto* d : groups[id]) {
            out += "<article><p><strong>" + show((*d)["severity"]) + " / " + show((*d)["disposition"]) + "</strong> — " + location((*d)["location"]) + " — " + show((*d)["hierarchy"]) + " — " + show((*d)["message"]) + "</p><strong>Evidence</strong><ul>";
            for (const auto& e : (*d)["evidence"].asArray()) out += "<li>" + show(e["kind"]) + ": " + show(e["text"]) + " — " + location(e["location"]) + extras(e, {"kind","text","location"}) + "</li>";
            out += "</ul><ul>";
            for (const char* k : {"confidence","proof_state","signals","next_step","fingerprint","root_cause","count","occurrences","origin","original_severity","requested_severity","severity_override_reason","upstream_code","upstream_severity","reviewer_decision","reviewer_comment","waiver"}) if (!(*d)[k].isNull()) out += "<li><strong>" + std::string(k) + ":</strong> " + htmlValue((*d)[k]) + "</li>";
            out += "</ul>" + extras(*d, {"rule_id","severity","disposition","location","hierarchy","message","evidence","confidence","proof_state","signals","next_step","fingerprint","root_cause","count","occurrences","origin","original_severity","requested_severity","severity_override_reason","upstream_code","upstream_severity","reviewer_decision","reviewer_comment","waiver"}) + "</article>";
        }
        out += "</details>";
    }
    out += "<h2>Inputs</h2>" + htmlValue(data["inputs"]) + "<h2>Coverage scopes and design objects</h2>" + htmlValue(data["coverage"]["scopes"]) + htmlValue(data["coverage"]["design_objects"]) + "<h2>Waivers</h2>" + htmlValue(data["waivers"]);
    out += "<h2>Report information</h2>" + htmlValue(data["schema"]) + htmlValue(data["tool"]) + extras(sum, {"execution_status","analysis_coverage","review_status","rules"}) + extras(data["coverage"], {"rules","scopes","design_objects"}) + extras(data, {"schema","tool","summary","inputs","coverage","waivers","diagnostics"}) + "</body></html>\n";
    return out;
}

// ------------------------------------------------------------------ SARIF
static const char* sarifLevel(Severity s) {
    switch (s) {
        case Severity::Info: return "note";
        case Severity::Warning: return "warning";
        default: return "error";
    }
}

json::Value Report::toSarif() const {
    using json::Value;
    Value run = Value::object();
    Value driver = Value::object();
    driver.set("name", "sverity").set("version", toolVersion_)
        .set("informationUri", "https://github.com/jerome-friendlygiant/sverity");
    Value rules = Value::array();
    std::map<std::string, size_t> ruleIdx;
    auto addRule = [&](const std::string& id, const std::string& title) {
        if (ruleIdx.count(id)) return;
        ruleIdx[id] = rules.asArray().size();
        Value r = Value::object();
        r.set("id", id);
        Value sd = Value::object();
        sd.set("text", title.empty() ? id : title);
        r.set("shortDescription", sd);
        r.set("fullDescription", sd);
        r.set("help", Value::object().set("text", ruleHelp(id)));
        rules.push(r);
    };
    const auto& cat = Catalog::embedded();
    for (auto& rc : ruleCoverage_) {
        auto* ri = cat.find(rc.ruleId);
        addRule(rc.ruleId, ri ? ri->title : "");
    }
    for (auto& d : diags_) {
        auto* ri = cat.find(d.ruleId);
        addRule(d.ruleId, ri ? ri->title : "");
    }
    driver.set("rules", rules);
    Value tool = Value::object();
    tool.set("driver", driver);
    run.set("tool", tool);

    Value results = Value::array();
    for (auto& d : diags_) {
        Value r = Value::object();
        r.set("ruleId", d.ruleId).set("ruleIndex", static_cast<uint64_t>(ruleIdx[d.ruleId]));
        r.set("level", sarifLevel(d.severity));
        Value msg = Value::object();
        msg.set("text", d.message + (d.nextStep.empty() ? "" : " Next step: " + d.nextStep));
        r.set("message", msg);
        auto location = [](const SourceLocation& source) {
            Value loc = Value::object();
            if (!source.file.empty()) {
                Value phys = Value::object();
                phys.set("artifactLocation", Value::object().set("uri", source.file));
                Value region = Value::object();
                if (source.line) region.set("startLine", source.line);
                if (source.line && source.column) region.set("startColumn", source.column);
                if (!region.asObject().empty()) phys.set("region", region);
                loc.set("physicalLocation", phys);
            }
            return loc;
        };
        if (!d.location.file.empty()) r.set("locations", Value::array().push(location(d.location)));
        Value related = Value::array();
        for (const auto& e : d.evidence) {
            auto loc = location(e.location);
            loc.set("id", static_cast<uint64_t>(related.asArray().size() + 1))
                .set("message", Value::object().set("text", e.kind + ": " + e.text));
            related.push(loc);
        }
        if (!related.asArray().empty()) r.set("relatedLocations", related);
        Value pf = Value::object();
        pf.set("sverity/v1", d.fingerprint);
        r.set("partialFingerprints", pf);
        if (d.disposition == Disposition::Waived) {
            Value sup = Value::object();
            sup.set("kind", d.waiver && d.waiver->sourceLocation.valid() ? "inSource" : "external").set("status", "accepted");
            if (d.waiver) sup.set("justification", d.waiver->reason);
            Value sups = Value::array();
            sups.push(sup);
            r.set("suppressions", sups);   // visible but marked suppressed; severity unchanged
        }
        Value props = Value::object();
        props.set("root_cause", d.rootCause).set("severity", toString(d.severity)).set("disposition", toString(d.disposition))
            .set("confidence", toString(d.confidence)).set("proofState", toString(d.proofState))
            .set("origin", d.origin).set("count", d.count).set("hierarchy", d.hierarchy);
        if (d.crossing) props.set("crossing", crossingJson(*d.crossing));
        if (d.waiver) props.set("waiver", Value::object().set("id", d.waiver->waiverId)
            .set("reason", d.waiver->reason).set("owner", d.waiver->owner)
            .set("source_location", locJson(d.waiver->sourceLocation)));
        if (d.originalSeverity) props.set("originalSeverity", toString(*d.originalSeverity));
        if (d.requestedSeverity) props.set("requestedSeverity", toString(*d.requestedSeverity));
        if (!d.severityOverrideReason.empty()) props.set("severityOverrideReason", d.severityOverrideReason);
        if (!d.upstreamCode.empty()) props.set("upstreamCode", d.upstreamCode);
        Value occ = Value::array();
        for (auto& h : d.occurrences) occ.push(h);
        props.set("occurrences", occ);
        r.set("properties", props);
        results.push(r);
    }
    run.set("results", results);
    Value props = Value::object();
    props.set("executionStatus", toString(executionStatus())).set("analysisCoverage", toString(analysisCoverage()))
        .set("reviewStatus", toString(reviewStatus())).set("inputs", inputs_);
    const auto canonical = toJson();
    props.set("coverage", canonical["coverage"]).set("waivers", canonical["waivers"]);
    run.set("properties", props);
    Value inv = Value::object();
    inv.set("executionSuccessful", executionStatus() == ExecutionStatus::Success);
    if (!executionError_.empty()) {
        Value note = Value::object();
        Value m = Value::object();
        m.set("text", executionError_);
        note.set("message", m).set("level", "error");
        Value notes = Value::array();
        notes.push(note);
        inv.set("toolExecutionNotifications", notes);
    }
    Value invs = Value::array();
    invs.push(inv);
    run.set("invocations", invs);
    Value runs = Value::array();
    runs.push(run);
    Value root = Value::object();
    root.set("$schema", "https://json.schemastore.org/sarif-2.1.0.json").set("version", "2.1.0").set("runs", runs);
    return root;
}

// ------------------------------------------------------------------- text
std::string Report::ruleSummary() const {
    struct Row { std::string id, state; uint32_t counts[8] = {}; };
    std::map<std::string, Row> rows;
    for (const auto& rc : ruleCoverage_) { auto& r = rows[rc.ruleId]; r.id = rc.ruleId; r.state = toString(rc.state); }
    for (const auto& d : diags_) {
        auto& r = rows[d.ruleId]; r.id = d.ruleId; if (r.state.empty()) r.state = "NOT_RUN";
        int i = d.severity == Severity::Fatal ? 0 : d.severity == Severity::Error ? 1 : d.severity == Severity::Warning ? 2 : 3;
        int j = d.disposition == Disposition::Open ? 4 : d.disposition == Disposition::ReviewRequired ? 5 : d.disposition == Disposition::Resolved ? 6 : 7;
        r.counts[i] += d.count; r.counts[j] += d.count;
    }
    std::vector<Row> sorted; for (const auto& [id, row] : rows) sorted.push_back(row);
    std::sort(sorted.begin(), sorted.end(), [](const Row& a, const Row& b) {
        for (int i = 0; i < 4; ++i) if ((a.counts[i] != 0) != (b.counts[i] != 0)) return a.counts[i] != 0;
        return a.id < b.id;
    });
    std::ostringstream out; out << "\n== Per-rule summary ==\nRULE STATE FATAL ERROR WARNING INFO OPEN REVIEW_REQUIRED RESOLVED WAIVED\n";
    for (const auto& r : sorted) { out << r.id << ' ' << r.state; for (auto n : r.counts) out << ' ' << n; out << '\n'; }
    return out.str();
}

std::string Report::statistics() const {
    std::map<std::string, std::vector<uint64_t>> counts;
    for (const auto& d : diags_) {
        auto& row = counts.try_emplace(d.ruleId, 4, 0).first->second;
        size_t i = d.severity == Severity::Fatal ? 0 : d.severity == Severity::Error ? 1 : d.severity == Severity::Warning ? 2 : 3;
        row[i] += d.count;
    }
    std::vector<std::pair<std::string, std::vector<uint64_t>>> rows(counts.begin(), counts.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    std::ostringstream out;
    out << "\n== Statistics (occurrences, including waived) ==\nRULE FATAL ERROR WARNING INFO\n";
    for (const auto& [id, row] : rows) {
        out << id;
        for (auto count : row) out << ' ' << count;
        out << '\n';
    }
    return out.str();
}

std::string Report::toText(bool color, size_t maxDiagnostics, const std::string& completeReportHint, bool group, bool verbose, bool strict, int extra, bool showStatistics) const {
    auto paint = [&](Severity s, const char* txt) -> std::string {
        if (!color) return txt;
        const char* c = s == Severity::Info ? "\x1b[36m" : s == Severity::Warning ? "\x1b[33m" : "\x1b[31m";
        return std::string(c) + txt + "\x1b[0m";
    };
    std::ostringstream o;
    const int exit = exitCode(strict) | extra;
    const bool noOpenFindings = reviewStatus() == ReviewStatus::Pass && analysisCoverage() == AnalysisCoverage::Incomplete;
    const std::string review = noOpenFindings ? "NO_OPEN_FINDINGS" : toString(reviewStatus());
    std::string exitMeaning;
    for (auto [bit, name] : {std::pair<int, const char*>{1, "FAIL"}, {2, "REVIEW_REQUIRED"}, {4, "INCOMPLETE"}, {8, "execution ERROR"}})
        if (exit & bit) exitMeaning += (exitMeaning.empty() ? "" : " + ") + std::string(name);
    if (!exitMeaning.empty()) exitMeaning += "; ";
    const Severity verdict = reviewStatus() == ReviewStatus::Fail || (extra & 8) ? Severity::Error :
                             reviewStatus() == ReviewStatus::Pass && analysisCoverage() != AnalysisCoverage::Incomplete ? Severity::Info : Severity::Warning;
    o << "Result: " << paint(verdict, review.c_str()) << ", exit " << exit << " (" << exitMeaning << (strict ? "strict" : "violations") << " policy)\n"
      << "  execution  " << ((extra & 8) ? "ERROR" : toString(executionStatus())) << '\n'
      << "  coverage   " << toString(analysisCoverage()) << '\n'
      << "  review     " << review << (noOpenFindings ? " (coverage incomplete)" : "") << '\n'
      << "  findings   " << countSeverity(Severity::Fatal) << " FATAL, " << countSeverity(Severity::Error)
      << " ERROR, " << countSeverity(Severity::Warning) << " WARNING, " << countSeverity(Severity::Info)
      << " INFO (" << countDisposition(Disposition::Waived) << " waived)\n";
    if (diags_.size() > 20) {
        // On a large run, say where the volume is before the first finding.
        std::map<std::string, std::pair<int, uint64_t>> perRule;
        for (const auto& d : diags_) {
            auto& row = perRule[d.ruleId];
            row.first = std::max(row.first, sevRank(d.severity));
            row.second += d.count;
        }
        std::vector<std::pair<std::string, std::pair<int, uint64_t>>> rows(perRule.begin(), perRule.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            if (a.second.first != b.second.first) return a.second.first > b.second.first;
            return a.second.second != b.second.second ? a.second.second > b.second.second : a.first < b.first;
        });
        o << "  top rules ";
        for (size_t i = 0; i < rows.size() && i < 5; ++i) o << (i ? ", " : " ") << rows[i].first << " x" << rows[i].second.second;
        o << (rows.size() > 5 ? " (" + std::to_string(rows.size()) + " rules; see --summary)" : "") << '\n';
    }
    o << '\n';
    std::set<std::pair<std::string, std::string>> adviceShown;
    auto details = [&](const Diagnostic& d, bool representative = false) {
        if (!verbose && !representative && d.severity == Severity::Info) return;
        for (const auto& e : d.evidence) {
            std::string label;
            if (e.kind == "launch_register") label = "launch";
            else if (e.kind == "capture_register") label = "capture";
            else if (e.kind == "source_domain") label = "launch clock";
            else if (e.kind == "destination_domain") label = "capture clock";
            else if (e.kind == "domain_provenance") label = "clocks";
            else if (e.kind == "data_path") label = "path";
            else if (e.kind == "trust_model") label = "assumption";
            else if (e.kind == "proof_obligation") label = "obligation";
            else if (e.kind == "path_step") label = "step";
            else if (e.kind == "cycle_step") label = "step";
            else if (verbose) label = e.kind;
            else continue;
            std::string_view text = e.text;
            constexpr std::string_view redundant = "relationship between the two clocks: ";
            if (label == "clocks" && text.rfind(redundant, 0) == 0) text.remove_prefix(redundant.size());
            o << "  " << label << ": " << text;
            if (d.crossing && e.kind == "launch_register") o << " [" << d.crossing->launch.edge << ']';
            if (d.crossing && e.kind == "capture_register") o << " [" << d.crossing->capture.edge << ']';
            if (e.location.valid()) o << " @" << e.location.file << ':' << e.location.line << ':' << e.location.column;
            o << '\n';
            if (!e.sourceLine.empty() && e.kind != "path_step") o << "    | " << e.sourceLine << '\n';
        }
        if (d.waiver) {
            o << "  waived by " << d.waiver->waiverId << " (" << d.waiver->owner << "): " << d.waiver->reason;
            if (d.waiver->sourceLocation.valid()) o << " @" << d.waiver->sourceLocation.file << ':' << d.waiver->sourceLocation.line << ':' << d.waiver->sourceLocation.column;
            o << '\n';
        }
        // The compact view gives each rule's advice once; a repeat adds length, not information.
        if (!d.nextStep.empty() && (verbose || representative || adviceShown.emplace(d.ruleId, d.nextStep).second))
            o << "  help: " << d.nextStep << '\n';
        if (verbose) {
            if (d.crossing) o << "  crossing: " << crossingJson(*d.crossing).dump(-1) << '\n';
            if (!d.hierarchy.empty()) o << "  hierarchy: " << d.hierarchy << '\n';
            o << "  occurrences: " << d.count;
            for (const auto& path : d.occurrences) o << ' ' << path;
            o << '\n';
            if (!d.signals.empty()) {
                o << "  signals:";
                for (const auto& signal : d.signals) o << ' ' << signal;
                o << '\n';
            }
            o << "  confidence: " << toString(d.confidence) << " | proof: " << toString(d.proofState)
              << "\n  fingerprint: " << d.fingerprint << " | root cause: " << d.rootCause << '\n';
            if (!d.reviewerDecision.empty()) o << "  decision: " << d.reviewerDecision << '\n';
            if (!d.reviewerComment.empty()) o << "  comment: " << d.reviewerComment << '\n';
        }
    };
    if (group && !completeReportHint.empty()) {
        struct ConsoleGroup { std::string id; std::vector<const Diagnostic*> items; uint64_t severities[4] = {}, dispositions[4] = {}; uint64_t waived = 0, downgradedError = 0; Severity worst = Severity::Info; };
        std::map<std::string, ConsoleGroup> groups;
        uint64_t total = 0;
        for (const auto& d : diags_) {
            const std::string id = d.rootCause.empty() ? "rc-" + computeFingerprint(d) : d.rootCause;
            auto& g = groups[id]; g.id = id; g.items.push_back(&d);
            g.severities[static_cast<size_t>(d.severity)] += d.count;
            g.dispositions[static_cast<size_t>(d.disposition)] += d.count;
            if (d.disposition == Disposition::Waived) g.waived += d.count;
            if (d.originalSeverity && *d.originalSeverity >= Severity::Error && d.severity < Severity::Error) g.downgradedError += d.count;
            if (d.severity > g.worst) g.worst = d.severity;
            total += d.count;
        }
        std::vector<const ConsoleGroup*> ordered;
        for (const auto& [id, g] : groups) ordered.push_back(&g);
        std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
            if (a->worst != b->worst) return a->worst > b->worst;
            auto count = [](const auto* g) { return g->severities[0] + g->severities[1] + g->severities[2] + g->severities[3]; };
            return count(a) != count(b) ? count(a) > count(b) : a->id < b->id;
        });
        o << "Console grouped: " << total << " occurrences in " << groups.size() << " root-cause groups; all findings are in "
          << completeReportHint << "; match each group ID to root_cause there. ERROR and FATAL groups appear first.\n";
        size_t shown = 0;
        for (const auto* g : ordered) {
            if (maxDiagnostics && shown >= maxDiagnostics && g->worst < Severity::Error) continue;
            ++shown;
            const Diagnostic* rep = *std::max_element(g->items.begin(), g->items.end(), [](const auto* a, const auto* b) { return a->severity < b->severity; });
            uint64_t count = g->severities[0] + g->severities[1] + g->severities[2] + g->severities[3];
            if (rep->location.valid()) o << rep->location.file << ':' << rep->location.line << ':' << rep->location.column << ": ";
            std::string mix;
            auto add = [&](const char* name, uint64_t n) { if (n) mix += (mix.empty() ? "" : ", ") + std::string(name) + "=" + std::to_string(n); };
            size_t levels = 0;
            for (auto n : g->severities) levels += n != 0;
            if (levels > 1) { add("INFO", g->severities[0]); add("WARNING", g->severities[1]); add("ERROR", g->severities[2]); add("FATAL", g->severities[3]); }
            add("WAIVED", g->waived); add("DOWNGRADED_ERROR", g->downgradedError);
            o << paint(g->worst, toString(g->worst)) << " [" << rep->ruleId << "] " << count << (count == 1 ? " occurrence" : " occurrences");
            if (!mix.empty()) o << " (" << mix << ")";
            o << ": " << rep->message;
            if (rep->originalSeverity && *rep->originalSeverity != rep->severity) o << " (was " << toString(*rep->originalSeverity) << ")";
            o << "  [" << g->id << "]\n";
            if (g->items.size() > 1 || rep->disposition != Disposition::Open) {
                std::string members;
                auto member = [&](const char* name, uint64_t n) { if (n) members += "; " + std::string(name) + "=" + std::to_string(n); };
                member("OPEN", g->dispositions[0]); member("REVIEW_REQUIRED", g->dispositions[1]);
                member("RESOLVED", g->dispositions[2]); member("WAIVED", g->dispositions[3]);
                o << "  members: " << g->items.size() << (g->items.size() == 1 ? " record" : " records") << members
                  << "; representative <" << toString(rep->disposition) << ">\n";
            }
            details(*rep, true);
        }
        if (shown < ordered.size()) o << "... output truncated: " << (ordered.size() - shown) << " more root-cause groups; see " << completeReportHint << ".\n";
    } else {
        size_t shown = 0;
        std::vector<const Diagnostic*> ordered;
        for (const auto& d : diags_) ordered.push_back(&d);
        std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
            if (a->severity != b->severity) return a->severity > b->severity;
            return std::tie(a->location.file, a->location.line, a->location.column, a->ruleId, a->fingerprint, a->disposition) <
                   std::tie(b->location.file, b->location.line, b->location.column, b->ruleId, b->fingerprint, b->disposition);
        });
        for (const auto* item : ordered) {
            const auto& d = *item;
            if (maxDiagnostics && shown >= maxDiagnostics) break;
            shown++;
            if (d.location.valid()) o << d.location.file << ':' << d.location.line << ':' << d.location.column << ": ";
            o << paint(d.severity, toString(d.severity)) << " [" << d.ruleId << "] " << d.message;
            if (d.originalSeverity && *d.originalSeverity != d.severity) o << " (was " << toString(*d.originalSeverity) << ")";
            if (d.count > 1) o << " (x" << d.count << ")";
            // OPEN and REVIEW_REQUIRED are the expected states; only a closed one changes how to read the line.
            if (verbose || d.disposition == Disposition::Waived || d.disposition == Disposition::Resolved)
                o << "  <" << toString(d.disposition) << ">";
            o << '\n';
            details(d);
        }
        if (shown < diags_.size())
            o << "... output truncated: " << (diags_.size() - shown) << " more diagnostic group(s) not shown here; "
              << (completeReportHint.empty() ? "re-run without the console limit to see them" : "the complete set is in " + completeReportHint)
              << ".\n";
    }
    if (!diags_.empty()) o << '\n';
    o << "Rule help: sverity --explain <RULE-ID>\n";
    if (analysisCoverage() == AnalysisCoverage::Incomplete)
        o << "Coverage is INCOMPLETE; absence of findings is not evidence of safety.\n";
    if (verbose) {
        // A goal run disables every other rule: the list belongs in the report, its size here.
        json::Value shownInputs = inputs_;
        if (const auto disabled = inputs_["disabled_rules"].asArray().size(); disabled > 10)
            shownInputs.set("disabled_rules", std::to_string(disabled) + " rules (listed in the JSON report)");
        o << "\nRun context\ninputs: " << shownInputs.dump() << '\n';
        if (!inputs_["goals"].asArray().empty()) {
            o << "goal:              limited to ";
            bool first = true;
            for (const auto& goal : inputs_["goals"].asArray()) {
                if (!first) o << ", ";
                o << goal.asString(); first = false;
            }
            if (inputs_["goal_broadened"].asBool()) o << " (explicit enables broaden scope)";
            o << '\n';
        }
        for (const auto& mode : inputs_["case_analysis"].asArray())
            o << "analysed mode:     " << mode["object"].asString() << "=" << mode["value"].asInt() << '\n';
        if (!inputs_["case_analysis"].asArray().empty())
            o << "pruned branches:   " << inputs_["case_pruned_branches"].asInt() << "; mux expressions: " << inputs_["case_pruned_expressions"].asInt() << " (alternatives not covered)\n";
        for (auto& s : scopes_)
            if (s.coverage == AnalysisCoverage::Incomplete) {
                o << "INCOMPLETE scope '" << s.scope << "':";
                for (auto& r : s.reasons) o << "\n    - " << r;
                o << '\n';
            }
    }
    if (showStatistics) {
        o << statistics();
        o << "Result: " << paint(verdict, review.c_str()) << ", exit " << exit
          << " (" << exitMeaning << (strict ? "strict" : "violations") << " policy)\n";
    }
    return o.str();
}

} // namespace sverity
