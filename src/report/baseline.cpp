#include "sverity/baseline.h"

#include <algorithm>

namespace sverity {

BaselineContext baselineContextOf(const std::vector<std::string>& slangArgs, const std::string& deck, const std::vector<std::string>& elaboratedTops) {
    std::vector<std::string> tops = elaboratedTops;
    for (size_t i = 0; i < slangArgs.size() && tops.empty(); ++i) {
        const auto& a = slangArgs[i];
        if (a == "--top" || a == "-top") { if (i + 1 < slangArgs.size()) tops.push_back(slangArgs[++i]); }
        else if (a.rfind("--top=", 0) == 0) tops.push_back(a.substr(6));
    }
    std::sort(tops.begin(), tops.end());
    std::string joined;
    for (const auto& t : tops) joined += (joined.empty() ? "" : ",") + t;
    return {joined.empty() ? "(not named)" : joined, deck, true};
}

json::Value baselineToJson(const Report& report, const BaselineContext& context) {
    json::Value root = json::Value::object();
    root.set("schema", "sverity-baseline/1");
    root.set("context", json::Value::object().set("top", context.top).set("deck", context.deck));
    json::Value list = json::Value::array();
    for (const auto& e : report.baselineEntries()) {
        json::Value o = json::Value::object();
        o.set("rule_id", e.rule).set("severity", e.severity).set("disposition", e.disposition).set("file", e.file)
            .set("line", static_cast<uint64_t>(e.line)).set("fingerprint", e.fingerprint).set("key", e.key);
        if (!e.scope.empty()) o.set("scope", e.scope);
        if (!e.anchor.empty()) o.set("anchor", e.anchor);
        list.push(o);
    }
    root.set("findings", list);
    return root;
}

bool parseBaseline(const std::string& text, BaselineFile& out, std::string& error) {
    auto parsed = json::parse(text);
    if (!parsed.ok || !parsed.value.isObject()) { error = "is not a JSON object"; return false; }
    const auto& root = parsed.value;
    const std::string schema = root["schema"].asString();
    const bool written = schema == "sverity-baseline/1";
    if (!written && schema != "sverity-report/1") { error = "is neither an sverity-baseline/1 file nor an sverity-report/1 JSON report"; return false; }
    const auto& list = root[written ? "findings" : "diagnostics"];
    if (!list.isArray()) { error = std::string("has no '") + (written ? "findings" : "diagnostics") + "' list"; return false; }
    for (const auto& d : list.asArray()) {
        if (!d["fingerprint"].isString() || d["fingerprint"].asString().empty()) { error = "holds a finding without a fingerprint"; return false; }
        BaselineEntry e;
        e.fingerprint = d["fingerprint"].asString();
        e.rule = d["rule_id"].asString();
        if (written) {
            e.file = d["file"].asString();
            e.line = static_cast<uint32_t>(d["line"].asInt());
            e.key = d["key"].asString();
            e.anchor = d["anchor"].asString();
            e.scope = d["scope"].asString();
            e.severity = d["severity"].asString();
            e.disposition = d["disposition"].asString();
        } else {
            e.file = d["location"]["file"].asString();
            e.line = static_cast<uint32_t>(d["location"]["line"].asInt());
            e.severity = d["severity"].asString();
            e.disposition = d["disposition"].asString();
            e.scope = d["scope"].asString();
            if (!d["signals"].asArray().empty()) e.anchor = d["signals"].asArray()[0].asString();
        }
        out.entries.push_back(std::move(e));
    }
    if (written) {
        out.context.top = root["context"]["top"].asString();
        out.context.deck = root["context"]["deck"].asString();
    } else {
        std::vector<std::string> args;
        for (const auto& a : root["inputs"]["slang_args"].asArray()) args.push_back(a.asString());
        std::vector<std::string> tops;
        for (const auto& t : root["inputs"]["elaborated_tops"].asArray()) tops.push_back(t.asString());
        out.context = baselineContextOf(args, root["inputs"]["deck"].asString(), tops);
        out.context.known = root["inputs"].isObject();
    }
    return true;
}

std::string baselineMismatch(const BaselineContext& baseline, const BaselineContext& run) {
    std::string why;
    if (!baseline.known) return why;
    if (baseline.top != run.top) why += "top '" + baseline.top + "' (this run: '" + run.top + "')";
    if (baseline.deck != run.deck) why += (why.empty() ? "" : ", ") + std::string("deck '") + (baseline.deck.empty() ? "(none)" : baseline.deck) +
                                          "' (this run: '" + (run.deck.empty() ? "(none)" : run.deck) + "')";
    return why;
}

} // namespace sverity
