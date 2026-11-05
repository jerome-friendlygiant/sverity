#include "sverity/waiver.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include "sverity/catalog.h"
#include "sverity/frontend.h"
#include "slang/driver/Driver.h"
#include "slang/parsing/Lexer.h"
#include "slang/text/SourceManager.h"

#include "sverity/json.h"

namespace sverity {

/// Blank or a stand-in nobody can be held to: a waiver needs a real owner and reason.
static bool isPlaceholder(const std::string& value) {
    std::string text;
    for (char c : value)
        if (!std::isspace(static_cast<unsigned char>(c))) text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* stub : {"", "<required>", "tbd", "todo", "fixme", "xxx", "n/a", "na", "none", "unknown"})
        if (text == stub) return true;
    return false;
}

// Use slang's lexer over its already loaded source-manager buffers. This is not an HDL
// parser and does not open files; strings, block comments and skipped branches stay lexical.
WaiverSet sourceWaivers(Design& design, const std::string& root) {
    WaiverSet out;
    auto& sm = design.driver->sourceManager;
    std::set<std::pair<std::string, size_t>> seen;
    for (auto buffer : sm.getAllBuffers()) {
        auto kind = sm.getBufferKind(buffer);
        if (kind != slang::SourceManager::BufferKind::DesignFile &&
            kind != slang::SourceManager::BufferKind::IncludeFile &&
            kind != slang::SourceManager::BufferKind::LibraryFile) continue;
        auto text = sm.getSourceText(buffer);
        if (text.empty()) continue;
        auto path = sm.getFullPath(buffer).lexically_normal();
        auto base = root.empty() ? std::filesystem::current_path() : std::filesystem::absolute(root).lexically_normal();
        auto file = path.lexically_relative(base).generic_string();
        slang::BumpAllocator alloc;
        slang::Diagnostics diagnostics;
        slang::parsing::LexerOptions options;
        options.maxErrors = UINT32_MAX; // keep scanning comments even in erroneous source
        slang::parsing::Lexer lexer({text, sm.getLibraryFor(buffer), buffer}, alloc, diagnostics, sm, options);
        uint32_t previousLine = 0;
        while (true) {
            auto token = lexer.lex();
            for (const auto& trivia : token.trivia()) {
                auto raw = trivia.getRawText();
                if (!raw.starts_with("//") && !raw.starts_with("/*")) continue;
                auto body = raw.substr(2);
                auto start = body.find_first_not_of(" \t");
                if (start == std::string_view::npos || !body.substr(start).starts_with("sverity-waive")) continue;
                size_t offset = static_cast<size_t>(raw.data() - text.data());
                if (!seen.emplace(file, offset).second) continue;
                slang::SourceLocation loc(buffer, offset);
                Waiver w;
                w.sourceLocation = {file, static_cast<uint32_t>(sm.getLineNumber(loc)),
                                   static_cast<uint32_t>(sm.getColumnNumber(loc))};
                w.id = file + ":" + std::to_string(w.sourceLocation.line) + ":" + std::to_string(w.sourceLocation.column);
                w.file = file;
                w.line = previousLine == w.sourceLocation.line ? previousLine :
                    static_cast<uint32_t>(sm.getLineNumber(token.location()));
                std::istringstream in(std::string(body.substr(start)));
                std::string marker;
                in >> marker >> w.rule;
                if (marker != "sverity-waive" || !raw.starts_with("//")) w.schemaError = "expected // sverity-waive RULE key=\"value\" ...";
                std::set<std::string> keys;
                while (in >> std::ws && in.peek() != EOF) {
                    std::string key, value;
                    while (in.peek() != EOF && in.peek() != '=' && !std::isspace(in.peek())) key += char(in.get());
                    if (in.get() != '=' || in.peek() != '"' || !(in >> std::quoted(value)) ||
                        !keys.insert(key).second) { w.schemaError = "malformed or duplicate attribute"; break; }
                    if (key == "owner") w.owner = value;
                    else if (key == "reason") w.reason = value;
                    else if (key == "expires") w.expires = value;
                    else if (key == "signal") w.signal = value;
                    else if (key == "hierarchy") w.hierarchy = value;
                    else { w.schemaError = "unknown attribute '" + key + "'"; break; }
                    if (in.peek() != EOF && !std::isspace(in.peek())) { w.schemaError = "attributes need whitespace"; break; }
                }
                if (previousLine != w.sourceLocation.line && token.kind == slang::parsing::TokenKind::EndOfFile)
                    w.schemaError = "no following token in this file";
                out.mutableWaivers().push_back(std::move(w));
            }
            if (token.kind == slang::parsing::TokenKind::EndOfFile) break;
            previousLine = static_cast<uint32_t>(sm.getLineNumber(token.location()));
        }
    }
    return out;
}

json::Value waiverTemplate(const Report& report) {
    auto result = json::Value::object(), entries = json::Value::array();
    result.set("note", "Waiving a finding does not prove it; unproved findings remain unproved. Replace every <REQUIRED> owner and reason.");
    for (const auto& d : report.diagnostics()) {
        if (d.disposition == Disposition::Waived || d.disposition == Disposition::Resolved) continue;
        auto entry = json::Value::object();
        entry.set("rule", d.ruleId).set("fingerprint", d.fingerprint).set("owner", "<REQUIRED>")
            .set("reason", "<REQUIRED>");
        if (!d.location.file.empty()) entry.set("file", d.location.file);
        if (d.location.line) entry.set("line", static_cast<int64_t>(d.location.line));
        entries.push(entry);
    }
    return result.set("waivers", entries);
}

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
    return !g.empty() && std::all_of(g.begin(), g.end(), [](char c) { return c == '*' || c == '?'; });
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
    if (d.ruleId != w.rule && std::find(d.alsoRules.begin(), d.alsoRules.end(), w.rule) == d.alsoRules.end()) return false;
    if (!w.fingerprint.empty() && w.fingerprint != d.fingerprint) return false;
    if (!w.file.empty() && (w.sourceLocation.valid() ? w.file != d.location.file : !globMatch(w.file, d.location.file))) return false;
    if (w.line && w.line != d.location.line) return false;
    if (!w.signal.empty()) {
        bool any = false;
        for (auto& s : d.signals) any = any || globMatch(w.signal, s);
        if (!any) return false;
        // A finding that groups several sources is accepted for its destination or for all of its sources, never for one of them.
        if (!d.groupedSources.empty() && !globMatch(w.signal, d.signals.front()) &&
            !std::all_of(d.groupedSources.begin(), d.groupedSources.end(), [&](const std::string& s) { return globMatch(w.signal, s); }))
            return false;
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
        d.location = w.sourceLocation;
        d.ruleId = rule;
        d.severity = Severity::Warning;
        d.identityArguments = {w.id, w.rule, w.fingerprint, w.file, std::to_string(w.line), w.hierarchy, w.signal, state};
        d.message = "Waiver '" + w.id + "' " + msg;
        d.confidence = Confidence::Unresolved;
        d.nextStep = "Fix, renew or remove the waiver; the waived finding (if any) stays open until then.";
        d.evidence.push_back({"waiver_scope", describe(w), {}});
        d.applyDefaultDisposition();
        extra.push_back(std::move(d));
        WaiverRecord r{w.id, w.rule, w.reason, w.owner, w.expires, describe(w), state, 0};
        report.addWaiverRecord(r);
    };
    std::map<std::string, size_t> ids, scopes;
    for (const auto& w : set.waivers()) { ids[w.id]++; scopes[describe(w)]++; }
    for (const auto& w : set.waivers()) {
        if (ids[w.id] > 1 || scopes[describe(w)] > 1) {
            problem(w, "CST-006", "invalid", "duplicates the id or scope of another waiver; not applied");
            continue;
        }
        if (!w.schemaError.empty()) {
            problem(w, "CST-006", "invalid", "is malformed (" + w.schemaError + "); not applied");
            continue;
        }
        if (w.rule.empty() || w.rule.find_first_of("*?") != std::string::npos || w.reason.empty() ||
            isPlaceholder(w.owner) || isPlaceholder(w.reason) ||
            (!Catalog::embedded().find(w.rule) &&
             !std::any_of(report.diagnostics().begin(), report.diagnostics().end(),
                          [&](const auto& d) { return d.ruleId == w.rule; }))) {
            problem(w, "CST-006", "invalid", "needs a known exact rule ID and nonblank, non-placeholder reason and owner; not applied");
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
            if (d.severity == Severity::Fatal || (d.origin == "slang" && d.severity >= Severity::Error)) continue;
            // A finding that stands for several rules is waived once each has a waiver. A fingerprint names this exact finding,
            // absorbed rules included, so it covers all of them.
            if (!d.alsoRules.empty() && w.fingerprint.empty()) {
                if (!w.hierarchy.empty() && std::any_of(d.occurrences.begin(), d.occurrences.end(), [&](const std::string& o) { return !globMatch(w.hierarchy, o); }))
                    continue;
                if (std::find(d.waivedFacts.begin(), d.waivedFacts.end(), w.rule) == d.waivedFacts.end()) d.waivedFacts.push_back(w.rule);
                std::vector<std::string> missing;
                for (const auto& rule : d.alsoRules)
                    if (std::find(d.waivedFacts.begin(), d.waivedFacts.end(), rule) == d.waivedFacts.end()) missing.push_back(rule);
                if (std::find(d.waivedFacts.begin(), d.waivedFacts.end(), d.ruleId) == d.waivedFacts.end()) missing.push_back(d.ruleId);
                if (!missing.empty()) { matched += d.count; continue; }
            }
            if (w.hierarchy.empty()) {
                d.disposition = Disposition::Waived;     // severity, evidence and identity are untouched
                d.waiver = WaiverInfo{w.id, w.reason, w.owner, w.expires, describe(w), w.sourceLocation};
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
                d.waiver = WaiverInfo{w.id, w.reason, w.owner, w.expires, describe(w), w.sourceLocation};
                matched += d.count;
                continue;
            }
            Diagnostic part = d;
            part.occurrences = hit;
            part.hierarchy = hit.front();
            part.count = static_cast<uint32_t>(hit.size());
            part.disposition = Disposition::Waived;
            part.waiver = WaiverInfo{w.id, w.reason, w.owner, w.expires, describe(w), w.sourceLocation};
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
    // Say what is still open on a finding only some of whose rules were waived. Advice is not part of its identity.
    for (auto& d : report.mutableDiagnostics()) {
        if (d.disposition == Disposition::Waived || d.waivedFacts.empty()) continue;
        std::string missing;
        for (const auto& rule : d.alsoRules)
            if (std::find(d.waivedFacts.begin(), d.waivedFacts.end(), rule) == d.waivedFacts.end()) missing += (missing.empty() ? "" : ", ") + rule;
        if (std::find(d.waivedFacts.begin(), d.waivedFacts.end(), d.ruleId) == d.waivedFacts.end()) missing += (missing.empty() ? "" : ", ") + d.ruleId;
        d.nextStep = "This finding stays open: it also stands for " + missing + ", which needs its own waiver, or waive the fingerprint. " + d.nextStep;
    }
    for (auto& d : extra) report.add(std::move(d));
    report.finalize();
}

} // namespace sverity
