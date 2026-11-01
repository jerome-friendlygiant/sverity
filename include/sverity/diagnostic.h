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
};

struct WaiverInfo {
    std::string waiverId;
    std::string reason;
    std::string owner;
    std::string expires;    // ISO date or empty
    std::string scope;      // human-readable description of the matching criteria
};

struct Diagnostic {
    std::string ruleId;                 // stable SVerity rule ID (e.g. "LNT-001")
    Severity severity = Severity::Info; // never rewritten by waivers
    std::string message;
    SourceLocation location;
    std::string hierarchy;              // hierarchical path of the (first) occurrence
    std::vector<Evidence> evidence;
    std::vector<std::string> signals;   // affected signals / clocks / resets
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

    /// Default disposition implied by severity/confidence (Warning/unresolved -> REVIEW_REQUIRED).
    void applyDefaultDisposition();
};

std::string computeFingerprint(const Diagnostic&);

} // namespace sverity
