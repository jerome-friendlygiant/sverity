// Waivers: a waiver changes a finding's *disposition* to WAIVED. It never deletes the finding,
// never rewrites its severity, and never turns uncertainty into "safe" (RFC section 3).
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "sverity/report.h"

namespace sverity {

struct Waiver {
    std::string id;
    std::string rule;             // exact rule ID, required
    std::string fingerprint;      // exact finding identity
    std::string file;             // glob on the report-relative path
    uint32_t line = 0;            // 0 = any
    std::string hierarchy;        // glob on any occurrence hierarchy
    std::string signal;           // glob on any affected signal
    std::string messageContains;
    std::string reason;           // required
    std::string owner;            // required
    std::string expires;          // optional ISO date YYYY-MM-DD
    std::string schemaError;      // non-empty: malformed entry (unknown key, wrong type...); never applied
};

class WaiverSet {
public:
    /// Parses {"waivers":[{...}]}. Structural problems are returned in `error`.
    /// `source` names the file (used to make default waiver ids unique across files).
    static bool parse(std::string_view jsonText, WaiverSet& out, std::string& error, const std::string& source = "waivers");
    const std::vector<Waiver>& waivers() const { return waivers_; }
    std::vector<Waiver>& mutableWaivers() { return waivers_; }

private:
    std::vector<Waiver> waivers_;
};

/// Applies waivers to a finalized report. Expired, stale (matching nothing), overbroad or incomplete
/// waivers are NOT applied and each yields a WARNING (CST-006 / CST-022) so they stay reviewable.
/// `today` is an ISO date (YYYY-MM-DD).
void applyWaivers(Report& report, const WaiverSet& set, const std::string& today);

bool globMatch(std::string_view pattern, std::string_view text);
/// Strict calendar check for YYYY-MM-DD.
bool isValidIsoDate(const std::string& s);

} // namespace sverity
