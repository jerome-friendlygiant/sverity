#include "sverity/diagnostic.h"

#include <cstdio>

namespace sverity {

const char* toString(Severity s) {
    switch (s) {
        case Severity::Info: return "INFO";
        case Severity::Warning: return "WARNING";
        case Severity::Error: return "ERROR";
        case Severity::Fatal: return "FATAL";
    }
    return "?";
}
const char* toString(Disposition d) {
    switch (d) {
        case Disposition::Open: return "OPEN";
        case Disposition::ReviewRequired: return "REVIEW_REQUIRED";
        case Disposition::Resolved: return "RESOLVED";
        case Disposition::Waived: return "WAIVED";
    }
    return "?";
}
const char* toString(ProofState p) {
    switch (p) {
        case ProofState::NotApplicable: return "NOT_APPLICABLE";
        case ProofState::Proven: return "PROVEN";
        case ProofState::Counterexample: return "COUNTEREXAMPLE";
        case ProofState::Unknown: return "UNKNOWN";
        case ProofState::NotAttempted: return "NOT_ATTEMPTED";
    }
    return "?";
}
const char* toString(Confidence c) {
    switch (c) {
        case Confidence::Established: return "established";
        case Confidence::Heuristic: return "heuristic";
        case Confidence::Unresolved: return "unresolved";
        case Confidence::Informational: return "informational";
    }
    return "?";
}
std::optional<Severity> severityFromString(std::string_view s) {
    if (s == "INFO") return Severity::Info;
    if (s == "WARNING") return Severity::Warning;
    if (s == "ERROR") return Severity::Error;
    if (s == "FATAL") return Severity::Fatal;
    return std::nullopt;
}

void Diagnostic::applyDefaultDisposition() {
    if (disposition == Disposition::Waived || disposition == Disposition::Resolved) return;
    if (severity == Severity::Warning || confidence == Confidence::Unresolved ||
        confidence == Confidence::Heuristic)
        disposition = severity == Severity::Info ? Disposition::Open : Disposition::ReviewRequired;
    else
        disposition = Disposition::Open;
}

// Length framing prevents ambiguous concatenations. Hierarchy is deliberately excluded:
// exact duplicates across instances retain the existing occurrence grouping contract.
std::string findingIdentity(const Diagnostic& d) {
    std::string key;
    auto part = [&](std::string_view s) { key += std::to_string(s.size()) + ":"; key += s; };
    part("finding-v2"); part(d.ruleId); part(d.location.file);
    part(std::to_string(d.location.line)); part(std::to_string(d.location.column));
    part(d.origin); part(d.upstreamCode); part(d.identityKey);
    part(std::to_string(d.identityArguments.size()));
    for (const auto& arg : d.identityArguments) part(arg);
    part(std::to_string(d.signals.size()));
    for (const auto& signal : d.signals) part(signal);
    part(d.groupingUnit); part(std::to_string(d.groupingProcedureOrdinal));
    part(std::to_string(d.evidence.size()));
    for (const auto& e : d.evidence) {
        part(e.kind); part(e.location.file);
        part(std::to_string(e.location.line)); part(std::to_string(e.location.column));
    }
    return key;
}

// FNV-1a 64: stable across platforms and runs.
std::string computeFingerprint(const Diagnostic& d) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : findingIdentity(d)) { h ^= c; h *= 1099511628211ull; }
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

} // namespace sverity
