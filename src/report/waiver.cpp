#include "sverity/waiver.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "sverity/json.h"

namespace sverity {

bool globMatch(std::string_view p, std::string_view t) {
    size_t pi = 0, ti = 0, star = std::string_view::npos, mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) { pi++; ti++; }
        else if (pi < p.size() && p[pi] == '*') { star = pi++; mark = ti; }
        else if (star != std::string_view::npos) { pi = star + 1; ti = ++mark; }
        else return false;
    }
    while (pi < p.size() && p[pi] == '*') pi++;
    return pi == p.size();
}

bool WaiverSet::parse(std::string_view text, WaiverSet& out, std::string& error, const std::string& source) {
    auto pr = json::parse(text);
    if (!pr.ok) { error = pr.error; return false; }
    if (!pr.value.isObject() || !pr.value["waivers"].isArray()) {
        error = "waiver file needs a top-level 'waivers' array";
        return false;
    }
    out = WaiverSet{};
    static const char* known[] = {"id", "rule", "fingerprint", "file", "line", "hierarchy", "signal",
                                  "message_contains", "reason", "owner", "expires"};
    size_t n = 0;
    for (auto& w : pr.value["waivers"].asArray()) {
        Waiver x;
        n++;
        x.id = source + "#" + std::to_string(n);
        if (!w.isObject()) { x.schemaError = "entry is not a JSON object"; out.waivers_.push_back(std::move(x)); continue; }
        std::string err;
        for (auto& [k, v] : w.asObject()) {
            bool ok = false;
            for (auto* kn : known) ok = ok || k == kn;
            if (!ok) { err = "unknown key '" + k + "'"; break; }
            if (k == "line") {
                if (!v.isInt() || v.asInt() < 1 || v.asInt() > 0xFFFFFFFFll) { err = "'line' must be a positive 32-bit integer"; break; }
            } else if (!v.isString()) { err = "'" + k + "' must be a string"; break; }
        }
        x.schemaError = err;
        if (w["id"].isString() && !w["id"].asString().empty()) x.id = w["id"].asString();
        x.rule = w["rule"].asString();
        x.fingerprint = w["fingerprint"].asString();
        x.file = w["file"].asString();
        x.line = static_cast<uint32_t>(w["line"].isInt() ? w["line"].asInt() : 0);
        x.hierarchy = w["hierarchy"].asString();
        x.signal = w["signal"].asString();
        x.messageContains = w["message_contains"].asString();
        x.reason = w["reason"].asString();
        x.owner = w["owner"].asString();
        x.expires = w["expires"].asString();
        out.waivers_.push_back(std::move(x));
    }
    return true;
}

bool isValidIsoDate(const std::string& d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (size_t i = 0; i < d.size(); i++)
        if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(d[i]))) return false;
    int y = std::stoi(d.substr(0, 4)), m = std::stoi(d.substr(5, 2)), day = std::stoi(d.substr(8, 2));
    if (m < 1 || m > 12 || day < 1) return false;
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maxd = dim[m - 1] + (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0);
    return day <= maxd;
}

static bool onlyWildcards(const std::string& g) {
    return !g.empty() && std::all_of(g.begin(), g.end(), [](char c) { return c == '*'; });
}
static std::string describe(const Waiver& w) {
    std::string s = "rule=" + w.rule;
    if (!w.fingerprint.empty()) s += " fingerprint=" + w.fingerprint;
    if (!w.file.empty()) s += " file=" + w.file;
    if (w.line) s += " line=" + std::to_string(w.line);
    if (!w.hierarchy.empty()) s += " hierarchy=" + w.hierarchy;
    if (!w.signal.empty()) s += " signal=" + w.signal;
    if (!w.messageContains.empty()) s += " message~\"" + w.messageContains + "\"";
    return s;
}

static bool matchesBase(const Waiver& w, const Diagnostic& d) {
    if (d.ruleId != w.rule) return false;
    if (!w.fingerprint.empty() && w.fingerprint != d.fingerprint) return false;
    if (!w.file.empty() && !globMatch(w.file, d.location.file)) return false;
    if (w.line && w.line != d.location.line) return false;
    if (!w.signal.empty()) {
        bool any = false;
        for (auto& s : d.signals) any = any || globMatch(w.signal, s);
        if (!any) return false;
    }
    if (!w.messageContains.empty() && d.message.find(w.messageContains) == std::string::npos) return false;
    return true;
}

static bool incompleteOccurrences(const Diagnostic& d) {
    for (auto& e : d.evidence) if (e.kind == "occurrence_list_incomplete") return true;
    return false;
}

void applyWaivers(Report& report, const WaiverSet& set, const std::string& today) {
    report.finalize();
    std::vector<Diagnostic> extra;
    auto problem = [&](const Waiver& w, const char* rule, const char* state, const std::string& msg) {
        Diagnostic d;
        d.ruleId = rule;
        d.severity = Severity::Warning;
        d.message = "Waiver '" + w.id + "' " + msg;
        d.confidence = Confidence::Unresolved;
        d.nextStep = "Fix, renew or remove the waiver; the waived finding (if any) stays open until then.";
        d.evidence.push_back({"waiver_scope", describe(w), {}});
        d.applyDefaultDisposition();
        extra.push_back(std::move(d));
        WaiverRecord r{w.id, w.rule, w.reason, w.owner, w.expires, describe(w), state, 0};
        report.addWaiverRecord(r);
    };
    std::set<std::string> seenIds;
    for (const auto& w : set.waivers()) {
        if (!seenIds.insert(w.id).second) {
            problem(w, "CST-006", "invalid", "duplicates the id of another waiver; not applied");
            continue;
        }
        if (!w.schemaError.empty()) {
            problem(w, "CST-006", "invalid", "is malformed (" + w.schemaError + "); not applied");
            continue;
        }
        if (w.rule.empty() || w.rule.find_first_of("*?") != std::string::npos || w.reason.empty() ||
            w.owner.empty()) {
            problem(w, "CST-006", "invalid", "needs an exact rule ID, a reason and an owner; not applied");
            continue;
        }
        if (!w.expires.empty() && !isValidIsoDate(w.expires)) {
            problem(w, "CST-022", "invalid", "has an invalid expiry date '" + w.expires + "'; not applied");
            continue;
        }
        if (!w.expires.empty() && w.expires < today) {
            problem(w, "CST-022", "expired", "expired on " + w.expires + "; not applied");
            continue;
        }
        bool narrow = !w.fingerprint.empty() ||
                      (!w.file.empty() && !onlyWildcards(w.file)) ||
                      (!w.hierarchy.empty() && !onlyWildcards(w.hierarchy)) ||
                      (!w.signal.empty() && !onlyWildcards(w.signal));
        if (!narrow) {
            problem(w, "CST-022", "overbroad",
                    "has no narrowing criterion (needs fingerprint, file, hierarchy or signal); not applied");
            continue;
        }
        uint32_t matched = 0;
        bool skippedIncomplete = false;
        std::vector<Diagnostic> splits;
        for (auto& d : report.mutableDiagnostics()) {
            if (d.disposition == Disposition::Waived) continue;
            if (!matchesBase(w, d)) continue;
            if (w.hierarchy.empty()) {
                d.disposition = Disposition::Waived;     // severity, evidence and identity are untouched
                d.waiver = WaiverInfo{w.id, w.reason, w.owner, w.expires, describe(w)};
                matched += d.count;
                continue;
            }
            // Hierarchy-scoped waiver: match per occurrence and split the group so that only the
            // matching occurrences are waived. Coalesced slang findings do not list every instance,
            // so a hierarchy waiver cannot be applied to them safely.
            if (incompleteOccurrences(d)) { skippedIncomplete = true; continue; }
            std::vector<std::string> hit, rest;
            for (auto& o : d.occurrences) (globMatch(w.hierarchy, o) ? hit : rest).push_back(o);
            if (hit.empty()) continue;
            if (rest.empty()) {
                d.disposition = Disposition::Waived;
                d.waiver = WaiverInfo{w.id, w.reason, w.owner, w.expires, describe(w)};
                matched += d.count;
                continue;
            }
            Diagnostic part = d;
            part.occurrences = hit;
            part.hierarchy = hit.front();
            part.count = static_cast<uint32_t>(hit.size());
            part.disposition = Disposition::Waived;
            part.waiver = WaiverInfo{w.id, w.reason, w.owner, w.expires, describe(w)};
            matched += part.count;
            d.count = d.count > part.count ? d.count - part.count : 1;
            d.occurrences = rest;
            d.hierarchy = rest.front();
            splits.push_back(std::move(part));
        }
        for (auto& sp : splits) report.mutableDiagnostics().push_back(std::move(sp));
        if (matched == 0) {
            problem(w, "CST-022", "stale",
                    skippedIncomplete
                        ? "matches only findings whose instance list is incomplete (slang coalesced identical "
                          "instances); a hierarchy-scoped waiver cannot be applied safely - scope it by file/line"
                        : "matches no finding in this run (stale); review whether it is still needed");
            continue;
        }
        report.addWaiverRecord({w.id, w.rule, w.reason, w.owner, w.expires, describe(w), "applied", matched});
    }
    for (auto& d : extra) report.add(std::move(d));
    report.finalize();
}

} // namespace sverity
