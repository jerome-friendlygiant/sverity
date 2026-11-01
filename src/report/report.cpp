#include "sverity/report.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
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
    return path;
}

static int sevRank(Severity s) { return static_cast<int>(s); }

void Report::finalize() {
    // 1. Normalize paths (reports must not leak or depend on the build machine's absolute paths).
    for (auto& d : diags_) {
        d.location.file = relativize(d.location.file);
        for (auto& e : d.evidence) e.location.file = relativize(e.location.file);
    }
    // 2. Group *exact* duplicates (same rule/severity/location/message/origin/upstream code),
    //    preserving every occurrence's hierarchy and the count. Nothing is dropped.
    std::vector<Diagnostic> grouped;
    std::map<std::string, size_t> seen;
    for (auto& d : diags_) {
        if (d.occurrences.empty() && !d.hierarchy.empty()) d.occurrences.push_back(d.hierarchy);
        // Only *exact* duplicates are grouped: every field except hierarchy/occurrences must be equal.
        std::string key = std::string(toString(d.severity)) + "\x1f" + computeFingerprint(d) + "\x1f" +
                          d.origin + "\x1f" + d.upstreamCode + "\x1f" + toString(d.disposition) + "\x1f" +
                          (d.waiver ? d.waiver->waiverId : "") + "\x1f" + toString(d.confidence) + "\x1f" +
                          toString(d.proofState) + "\x1f" + d.nextStep + "\x1f" + d.reviewerDecision + "\x1f" +
                          d.reviewerComment;
        for (auto& e : d.evidence)
            key += "\x1e" + e.kind + "\x1f" + e.text + "\x1f" + e.location.file + ":" + std::to_string(e.location.line);
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
    for (auto& d : diags_) {
        std::sort(d.occurrences.begin(), d.occurrences.end());
        d.fingerprint = computeFingerprint(d);
    }
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
        if (rc.state != RuleState::Executed) return AnalysisCoverage::Incomplete;
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
        r.set("rule_id", rc.ruleId).set("state", toString(rc.state)).set("catalog_status", rc.catalogStatus)
            .set("objects_evaluated", rc.objectsEvaluated).set("findings", rc.findings);
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
            .set("message", d.message).set("location", locJson(d.location)).set("hierarchy", d.hierarchy)
            .set("confidence", toString(d.confidence)).set("proof_state", toString(d.proofState))
            .set("disposition", toString(d.disposition)).set("origin", d.origin);
        if (!d.upstreamCode.empty()) o.set("upstream_code", d.upstreamCode);
        if (!d.upstreamSeverity.empty()) o.set("upstream_severity", d.upstreamSeverity);
        json::Value ev = json::Value::array();
        for (auto& e : d.evidence) {
            json::Value x = json::Value::object();
            x.set("kind", e.kind).set("text", e.text).set("location", locJson(e.location));
            ev.push(x);
        }
        o.set("evidence", ev);
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
                .set("expires", d.waiver->expires).set("scope", d.waiver->scope);
            o.set("waiver", w);
        }
        ds.push(o);
    }
    root.set("diagnostics", ds);
    return root;
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
        if (d.location.valid()) {
            Value art = Value::object();
            art.set("uri", d.location.file);
            Value reg = Value::object();
            reg.set("startLine", d.location.line).set("startColumn", std::max<uint32_t>(1, d.location.column));
            Value phys = Value::object();
            phys.set("artifactLocation", art).set("region", reg);
            Value loc = Value::object();
            loc.set("physicalLocation", phys);
            Value locs = Value::array();
            locs.push(loc);
            r.set("locations", locs);
        }
        Value pf = Value::object();
        pf.set("sverity/v1", d.fingerprint);
        r.set("partialFingerprints", pf);
        if (d.disposition == Disposition::Waived) {
            Value sup = Value::object();
            sup.set("kind", "external").set("status", "accepted");
            if (d.waiver) sup.set("justification", d.waiver->reason);
            Value sups = Value::array();
            sups.push(sup);
            r.set("suppressions", sups);   // visible but marked suppressed; severity unchanged
        }
        Value props = Value::object();
        props.set("severity", toString(d.severity)).set("disposition", toString(d.disposition))
            .set("confidence", toString(d.confidence)).set("proofState", toString(d.proofState))
            .set("origin", d.origin).set("count", d.count).set("hierarchy", d.hierarchy);
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
        .set("reviewStatus", toString(reviewStatus()));
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
std::string Report::toText(bool color, size_t maxDiagnostics, const std::string& completeReportHint) const {
    auto paint = [&](Severity s, const char* txt) -> std::string {
        if (!color) return txt;
        const char* c = s == Severity::Info ? "\x1b[36m" : s == Severity::Warning ? "\x1b[33m" : "\x1b[31m";
        return std::string(c) + txt + "\x1b[0m";
    };
    std::ostringstream o;
    size_t shown = 0;
    for (auto& d : diags_) {
        if (maxDiagnostics && shown >= maxDiagnostics) break;
        shown++;
        if (d.location.valid()) o << d.location.file << ':' << d.location.line << ':' << d.location.column << ": ";
        o << paint(d.severity, toString(d.severity)) << " [" << d.ruleId << "] " << d.message;
        if (d.count > 1) o << " (x" << d.count << ")";
        o << "  <" << toString(d.disposition) << ">\n";
        if (!d.hierarchy.empty()) o << "    hierarchy: " << d.hierarchy << '\n';
        for (auto& e : d.evidence) {
            o << "    evidence(" << e.kind << "): " << e.text;
            if (e.location.valid()) o << " @" << e.location.file << ':' << e.location.line;
            o << '\n';
        }
        if (!d.signals.empty()) {
            o << "    signals:";
            for (auto& s : d.signals) o << ' ' << s;
            o << '\n';
        }
        if (d.confidence != Confidence::Established)
            o << "    confidence: " << toString(d.confidence) << '\n';
        if (d.waiver) o << "    waived by " << d.waiver->waiverId << " (" << d.waiver->owner << "): " << d.waiver->reason << '\n';
        if (!d.nextStep.empty()) o << "    next: " << d.nextStep << '\n';
    }
    if (shown < diags_.size())
        o << "... output truncated: " << (diags_.size() - shown) << " more diagnostic group(s) not shown here; "
          << (completeReportHint.empty() ? "re-run without the console limit to see them" : "the complete set is in " + completeReportHint)
          << ".\n";
    uint32_t cnt[6] = {0, 0, 0, 0, 0, 0};
    for (auto& rc : ruleCoverage_) cnt[static_cast<int>(rc.state)]++;
    o << "\n== SVerity summary ==\n";
    o << "execution_status:  " << toString(executionStatus()) << '\n';
    o << "analysis_coverage: " << toString(analysisCoverage()) << '\n';
    o << "review_status:     " << toString(reviewStatus()) << '\n';
    o << "diagnostics:       " << countSeverity(Severity::Fatal) << " FATAL, " << countSeverity(Severity::Error)
      << " ERROR, " << countSeverity(Severity::Warning) << " WARNING, " << countSeverity(Severity::Info)
      << " INFO (" << countDisposition(Disposition::Waived) << " waived)\n";
    o << "rules:             " << ruleCoverage_.size() << " catalogued: "
      << cnt[static_cast<int>(RuleState::Executed)] << " executed, " << cnt[static_cast<int>(RuleState::Partial)]
      << " partial, " << cnt[static_cast<int>(RuleState::Disabled)] << " disabled, "
      << cnt[static_cast<int>(RuleState::Unimplemented)] << " unimplemented, "
      << cnt[static_cast<int>(RuleState::NotRun)] << " not run, " << cnt[static_cast<int>(RuleState::Failed)]
      << " failed\n";
    for (auto& s : scopes_)
        if (s.coverage == AnalysisCoverage::Incomplete) {
            o << "INCOMPLETE scope '" << s.scope << "':";
            for (auto& r : s.reasons) o << "\n    - " << r;
            o << '\n';
        }
    if (analysisCoverage() == AnalysisCoverage::Incomplete)
        o << "NOTE: coverage is INCOMPLETE; absence of findings must not be read as safe.\n";
    return o.str();
}

} // namespace sverity
