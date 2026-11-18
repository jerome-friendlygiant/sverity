// Canonical report: every diagnostic, coverage record and waiver, plus the three
// orthogonal result dimensions (execution_status / analysis_coverage / review_status).
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sverity/diagnostic.h"
#include "sverity/json.h"

namespace sverity {

enum class ExecutionStatus { Success, Error };
enum class AnalysisCoverage { Complete, Incomplete };
enum class ReviewStatus { Pass, ReviewRequired, Fail };

/// What a baseline remembers of one finding (see Report::setBaseline).
struct BaselineEntry {
    std::string rule, file, scope, key, anchor, fingerprint, severity, disposition;
    uint32_t line = 0;
};

/// Which findings and coverage losses set an exit bit. Gate (default): defects and run-specific loss; an obligation or a gap
/// blocks only when new against the baseline. Strict: everything unresolved, partial or disabled blocks. Violations: only FAIL
/// and execution ERROR.
enum class ExitPolicy { Gate, Strict, Violations };
const char* toString(ExitPolicy);

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
    bool preview = false;            // outside the selected deck: runs if it can, findings stay, coverage not claimed
    bool degraded = false;           // partial on THIS design (a run-specific loss), not partial in the tool version
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
    const json::Value& inputs() const { return inputs_; }
    void setToolInfo(std::string version, std::string slangVersion, std::string catalogVersion);
    /// Files loaded independently of rule selection, for stable source identity.
    void addIdentityFile(std::string file) { identityFiles_.push_back(std::move(file)); }
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
    /// Run-specific loss only (elaboration, constraints, a failed, skipped or degraded rule). Under Strict a rule that is
    /// partial, unimplemented or disabled also counts. Tool-version coverage is toolCoverage().
    AnalysisCoverage analysisCoverage(ExitPolicy policy = ExitPolicy::Gate) const;
    /// Findings that block under `policy`. Obligation and NotAnalysed findings block under Strict, or when a baseline is set
    /// and their fingerprint is not in it.
    ReviewStatus reviewStatus(ExitPolicy policy = ExitPolicy::Gate) const;
    struct ToolCoverage { uint32_t complete = 0, total = 0, disabled = 0; };
    /// Rules of the selected deck (all rules without a deck) that ran to completion; static for a tool version.
    ToolCoverage toolCoverage() const;
    /// Sets the baseline (after finalize): each unwaived finding is matched to at most one entry, and a matched finding is BASELINED:
    /// reported, counted, never gating. Matching, in this order, each step on what the earlier ones left: the exact fingerprint; the
    /// same key (the fingerprint's identity without the line, with the enclosing definition), nearest line first; the same rule,
    /// file, definition and anchor (a signal or identity key; none, no match), nearest line first. The two nearest-line steps pair
    /// only entries and findings of one definition at most 200 lines apart. An entry matches once, so a second finding of the same
    /// kind is new, and it covers a finding only at or below its recorded severity: a finding that blocks where the entry did not
    /// (an INFO that became a WARNING, a gap that became a defect) is new.
    /// Any other finding is NEW: it gates as it would without a baseline, and an obligation or gap that is new blocks.
    void setBaseline(std::vector<BaselineEntry> entries);
    /// Fingerprints only (an exact match, nothing else).
    void setBaseline(std::set<std::string> fingerprints);
    /// The identity of the findings a baseline written now would hold: every one that is not waived or resolved.
    std::vector<BaselineEntry> baselineEntries() const;
    struct BaselineCounts { uint32_t entries = 0, fresh = 0, baselined = 0, byFingerprint = 0, byKey = 0, byAnchor = 0, fixed = 0; };
    /// Occurrences new and baselined (and the entries nothing matched: fixed since the baseline). Zero without a baseline.
    BaselineCounts baselineCounts() const;
    bool hasBaseline() const { return hasBaseline_; }
    /// An obligation or gap that blocks because a baseline is set and does not hold it.
    bool newAgainstBaseline(const Diagnostic& d) const;
    /// The policy the JSON, SARIF and HTML summaries state their review status and coverage under (the console and exit code take
    /// it as an argument). Recorded in `inputs.exit_policy` by the caller.
    void setExitPolicy(ExitPolicy policy) { policy_ = policy; }
    /// Definitions the elaborated design never instantiates: slang walks them only for diagnostics and lint does not analyse them.
    void setUninstantiatedDefinitions(std::vector<std::string> names) { uninstantiated_ = std::move(names); }
    uint32_t countDefects() const;        // occurrences that block by severity or heuristic, never obligations or gaps
    uint32_t countSeverity(Severity s) const;      // counts occurrences (grouped count summed)
    uint32_t countDisposition(Disposition d) const;
    /// Deterministic exit code bitmask: 1 FAIL, 2 REVIEW_REQUIRED, 4 INCOMPLETE, 8 execution ERROR (strict policy).
    int exitCode(ExitPolicy policy = ExitPolicy::Gate) const;

    // -------- writers
    std::string ruleSummary() const;
    json::Value toJson() const;
    json::Value toSarif() const;
    std::string toJsonl(ExitPolicy policy = ExitPolicy::Gate, int extra = 0) const;
    std::string statistics() const;
    std::string toHtml() const;
    /// `completeReportHint` names where the complete report was written (shown if the console is truncated).
    std::string toText(bool color = false, size_t maxDiagnostics = 0 /*0 = all*/,
                       const std::string& completeReportHint = {}, bool group = false, bool verbose = false,
                       ExitPolicy policy = ExitPolicy::Gate, int extra = 0, bool showStatistics = false) const;

private:
    std::string relativize(const std::string& path) const;

    std::vector<Diagnostic> diags_;
    std::vector<std::string> identityFiles_;
    std::vector<RuleCoverage> ruleCoverage_;
    std::vector<ScopeCoverage> scopes_;
    std::vector<WaiverRecord> waivers_;
    std::map<std::string, uint64_t> objectCounts_;
    json::Value inputs_ = json::Value::object();
    std::string executionError_;
    std::string toolVersion_, slangVersion_, catalogVersion_, pathRoot_;
    std::set<std::string> baseline_;
    bool hasBaseline_ = false;
    uint32_t baselineEntries_ = 0, baselineFixed_ = 0;
    ExitPolicy policy_ = ExitPolicy::Gate;
    std::vector<std::string> uninstantiated_;
    bool finalized_ = false;
};

} // namespace sverity
