// Canonical report: every diagnostic, coverage record and waiver, plus the three
// orthogonal result dimensions (execution_status / analysis_coverage / review_status).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "sverity/diagnostic.h"
#include "sverity/json.h"

namespace sverity {

enum class ExecutionStatus { Success, Error };
enum class AnalysisCoverage { Complete, Incomplete };
enum class ReviewStatus { Pass, ReviewRequired, Fail };

const char* toString(ExecutionStatus);
const char* toString(AnalysisCoverage);
const char* toString(ReviewStatus);

enum class RuleState {
    Executed,       // ran to completion; implements the full catalog title
    Partial,        // ran, but implements only a documented subset of the catalog title
    Disabled,       // disabled by configuration/profile (still listed)
    Unimplemented,  // registered in the catalog, no native implementation yet
    NotRun,         // implemented but could not run (e.g. design failed to load)
    Failed          // implementation threw / hit an internal invariant failure
};
const char* toString(RuleState);

struct RuleCoverage {
    std::string ruleId;
    RuleState state = RuleState::Unimplemented;
    std::string catalogStatus;       // proposed | specified | implemented | validated | blocked
    std::string reason;              // why disabled/unimplemented/failed
    uint64_t objectsEvaluated = 0;   // design objects the rule looked at
    uint32_t findings = 0;           // diagnostics (all severities) attributed to the rule
};

struct ScopeCoverage {
    std::string scope;               // e.g. "elaboration", "clock-extraction"
    AnalysisCoverage coverage = AnalysisCoverage::Complete;
    std::vector<std::string> reasons;
};

struct WaiverRecord {
    std::string id, rule, reason, owner, expires, scope;
    std::string state;               // "applied" | "stale" | "expired" | "overbroad" | "invalid"
    uint32_t matched = 0;
};

class Report {
public:
    // -------- population
    void add(Diagnostic d);
    void addRuleCoverage(RuleCoverage rc) { ruleCoverage_.push_back(std::move(rc)); }
    /// Marks an analysis scope INCOMPLETE and records why. Idempotent per (scope,reason).
    void markIncomplete(const std::string& scope, const std::string& reason);
    /// Ensures the scope is listed (as COMPLETE unless later marked incomplete).
    void declareScope(const std::string& scope);
    void setExecutionError(const std::string& why);
    void addWaiverRecord(WaiverRecord w) { waivers_.push_back(std::move(w)); }
    void setObjectCount(const std::string& name, uint64_t n) { objectCounts_[name] = n; }
    void addInput(const std::string& key, json::Value v) { inputs_.set(key, std::move(v)); }
    void setToolInfo(std::string version, std::string slangVersion, std::string catalogVersion);
    void setPathRoot(std::string root) { pathRoot_ = std::move(root); }

    /// Normalize paths, group exact duplicates, sort deterministically, compute fingerprints
    /// and per-rule finding counts. Never removes a diagnostic. Idempotent.
    void finalize();

    // -------- queries (valid after finalize())
    const std::vector<Diagnostic>& diagnostics() const { return diags_; }
    std::vector<Diagnostic>& mutableDiagnostics() { return diags_; }
    const std::vector<RuleCoverage>& ruleCoverage() const { return ruleCoverage_; }
    std::vector<RuleCoverage>& mutableRuleCoverage() { return ruleCoverage_; }
    const std::vector<ScopeCoverage>& scopes() const { return scopes_; }
    const std::vector<WaiverRecord>& waivers() const { return waivers_; }
    std::vector<WaiverRecord>& mutableWaivers() { return waivers_; }
    ExecutionStatus executionStatus() const;
    AnalysisCoverage analysisCoverage() const;
    ReviewStatus reviewStatus() const;
    uint32_t countSeverity(Severity s) const;      // counts occurrences (grouped count summed)
    uint32_t countDisposition(Disposition d) const;
    /// Deterministic exit code bitmask: 1 FAIL, 2 REVIEW_REQUIRED, 4 INCOMPLETE, 8 execution ERROR (strict policy).
    int exitCode(bool strict = true) const;

    // -------- writers
    json::Value toJson() const;
    json::Value toSarif() const;
    /// `completeReportHint` names where the complete report was written (shown if the console is truncated).
    std::string toText(bool color = false, size_t maxDiagnostics = 0 /*0 = all*/,
                       const std::string& completeReportHint = {}) const;

private:
    std::string relativize(const std::string& path) const;

    std::vector<Diagnostic> diags_;
    std::vector<RuleCoverage> ruleCoverage_;
    std::vector<ScopeCoverage> scopes_;
    std::vector<WaiverRecord> waivers_;
    std::map<std::string, uint64_t> objectCounts_;
    json::Value inputs_ = json::Value::object();
    std::string executionError_;
    std::string toolVersion_, slangVersion_, catalogVersion_, pathRoot_;
    bool finalized_ = false;
};

} // namespace sverity
