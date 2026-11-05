// Diagnostic data model implementing the non-negotiable diagnostic contract
// (RFC section 3): INFO/WARNING/ERROR/FATAL are never dropped; waived items keep
// their original severity; unknown is never "safe".
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sverity {

enum class Severity { Info, Warning, Error, Fatal };
enum class Disposition { Open, ReviewRequired, Resolved, Waived };
/// Formal-proof extension contract (RFC section 3). Structural rules use NotApplicable.
enum class ProofState { NotApplicable, Proven, Counterexample, Unknown, NotAttempted };
/// How strongly the finding is established.
enum class Confidence {
    Established,   // follows from the stated rule semantics on the elaborated design
    Heuristic,     // pattern/heuristic classification; requires review
    Unresolved,    // SVerity could not determine safety; requires review
    Informational  // context, statistics, classified-safe structure
};

const char* toString(Severity);
const char* toString(Disposition);
const char* toString(ProofState);
const char* toString(Confidence);
std::optional<Severity> severityFromString(std::string_view);

struct SourceLocation {
    std::string file;       // empty when unavailable
    uint32_t line = 0;      // 1-based; 0 when unavailable
    uint32_t column = 0;    // 1-based; 0 when unavailable
    bool valid() const { return !file.empty() && line > 0; }
    bool operator==(const SourceLocation&) const = default;
};

struct Evidence {
    std::string kind;       // e.g. "launch_register", "capture_register", "slang_note"
    std::string text;
    SourceLocation location;
    std::string sourceLine;   // the source text at `location`, for CDC findings; not part of finding identity
};

// Endpoints describe the representative launch/capture registers of a grouped crossing.
// Empty SDC names and invalid locations retain their existing unavailable meaning.
struct CrossingEndpoint {
    std::string signal;
    std::string clockDomain;
    std::string sdcClock;
    std::string edge = "unknown"; // posedge, negedge, both, unknown
    SourceLocation location;     // procedure holding the register
};

struct Crossing {
    CrossingEndpoint launch, capture;
    std::string relationship = "UNKNOWN"; // SAME, ASYNC, EXCLUSIVE, UNKNOWN
    uint64_t widthBits = 0;
    bool logicOnPath = false;
};

struct WaiverInfo {
    std::string waiverId;
    std::string reason;
    std::string owner;
    std::string expires;    // ISO date or empty
    std::string scope;      // human-readable description of the matching criteria
    SourceLocation sourceLocation;
};

struct Diagnostic {
    std::string ruleId;                 // stable SVerity rule ID (e.g. "LNT-001")
    Severity severity = Severity::Info; // never rewritten by waivers
    std::optional<Severity> originalSeverity;
    std::optional<Severity> requestedSeverity;
    std::string severityOverrideReason;
    std::string message;
    std::vector<std::string> identityArguments; // upstream argument values, path-normalized at finalize
    std::string identityKey;            // producer-owned semantic discriminator; never display prose
    SourceLocation location;
    std::string sourceLine;             // the source text at `location`, for CDC findings; not part of finding identity
    std::string hierarchy;              // hierarchical path of the (first) occurrence
    std::optional<Crossing> crossing; // shared CDC facts; absent for other diagnostics
    std::vector<Evidence> evidence;
    std::vector<std::string> signals;   // affected signals / clocks / resets
    std::vector<std::string> alsoRules; // enabled rules this primary finding also stands for: each keeps its own override and waiver
    std::vector<std::string> waivedFacts; // internal: rules among those that a waiver has covered so far
    std::vector<std::string> groupedSources; // a signal-scoped waiver must cover all of these, or the destination (signals[0])
    Confidence confidence = Confidence::Established;
    ProofState proofState = ProofState::NotApplicable;
    Disposition disposition = Disposition::Open;
    std::string nextStep;               // actionable next step
    std::string origin = "sverity";     // "sverity" or "slang"
    std::string upstreamCode;           // original upstream diagnostic code, e.g. slang "unused-net"
    std::string upstreamSeverity;       // original upstream severity text
    std::string reviewerDecision;       // optional
    std::string reviewerComment;        // optional
    std::optional<WaiverInfo> waiver;   // set when disposition == Waived
    std::vector<std::string> occurrences; // hierarchy paths of every grouped occurrence
    uint32_t count = 1;                 // number of exact-duplicate occurrences grouped here
    std::string fingerprint;            // stable identity (filled by Report::finalize)
    std::string rootCause;              // console grouping identity, not a waiver key
    std::string originalFile, originalMessage; // internal, before report-root relativization
    std::vector<std::string> originalEvidence, originalEvidenceFiles, originalIdentityArguments;
    bool groupingOriginCaptured = false;
    std::string groupingUnit;            // internal elaborated module/interface definition
    uint32_t groupingProcedureOrdinal = 0; // distinguishes procedures sharing a macro expansion

    /// Default disposition implied by severity/confidence (Warning/unresolved -> REVIEW_REQUIRED).
    void applyDefaultDisposition();
};

std::string findingIdentity(const Diagnostic&);
std::string computeFingerprint(const Diagnostic&);

} // namespace sverity
