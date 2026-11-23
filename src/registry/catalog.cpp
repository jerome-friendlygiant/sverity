#include "sverity/catalog.h"
#include <sstream>

namespace sverity::embedded { extern const unsigned char specs[]; }

#include <cstdio>
#include <cstdlib>

#include "sverity/json.h"

namespace sverity::embedded {
extern const unsigned char catalog_data[];
extern const std::size_t catalog_size;
} // namespace sverity::embedded

namespace sverity {

bool Catalog::parse(std::string_view text, Catalog& out, std::string& error) {
    auto pr = json::parse(text);
    if (!pr.ok) { error = pr.error; return false; }
    const auto& root = pr.value;
    if (!root["rules"].isArray()) { error = "catalog has no 'rules' array"; return false; }
    out = Catalog{};
    out.version_ = root["catalog_version"].asString();
    for (const auto& r : root["rules"].asArray()) {
        RuleInfo ri;
        ri.id = r["id"].asString();
        if (ri.id.empty()) { error = "rule without id"; return false; }
        if (out.index_.count(ri.id)) { error = "duplicate rule id " + ri.id; return false; }
        ri.category = ri.id.substr(0, ri.id.find('-'));
        ri.title = r["title"].asString();
        ri.notes = r["notes"].asString();
        ri.status = r["status"].asString();
        ri.wave = r["wave"].asString();
        ri.gap = r["gap"].asString();
        ri.deck = r["deck"].asString();
        ri.cdcDeck = r["cdc_deck"].asBool();
        for (const auto& c : r["classes"].asArray()) ri.classes.push_back(c.asString());
        for (const auto& o : r["obligations"].asArray()) {
            ObligationInfo info{o["id"].asString(), o["reason"].asString()};
            if (info.id.empty()) { error = ri.id + ": an obligation needs an id"; return false; }
            ri.obligations.push_back(std::move(info));
        }
        out.index_[ri.id] = out.rules_.size();
        out.rules_.push_back(std::move(ri));
    }
    return true;
}

const Catalog& Catalog::embedded() {
    static const Catalog c = [] {
        Catalog cat;
        std::string err;
        std::string_view text(reinterpret_cast<const char*>(embedded::catalog_data),
                              embedded::catalog_size);
        if (!parse(text, cat, err)) {
            std::fprintf(stderr, "sverity: internal error: embedded rule catalog is corrupt: %s\n", err.c_str());
            std::abort();
        }
        const std::string_view specs(reinterpret_cast<const char*>(embedded::specs));
        for (auto& rule : cat.rules_) {
            if (rule.category != "LNT" || !rule.title.empty()) continue;
            const auto row = "\n| `" + rule.id + "` | ";
            const auto start = specs.find(row);
            if (start != std::string_view::npos) {
                const auto title = start + row.size();
                const auto end = specs.find(" |\n", title);
                if (end != std::string_view::npos) rule.title = specs.substr(title, end - title);
            }
            if (rule.title.empty()) {
                std::fprintf(stderr, "sverity: missing embedded spec title for %s\n", rule.id.c_str());
                std::abort();
            }
        }
        return cat;
    }();
    return c;
}

static int deckOrder(std::string_view deck) { return deck == "core" ? 1 : deck == "domain" ? 2 : deck == "1.0" ? 3 : 0; }
bool isDeck(std::string_view name) { return deckOrder(name) != 0 || name == "lint" || name == "cdc"; }
bool inDeck(const RuleInfo& rule, std::string_view deck) {
    if (deck == "lint") return !rule.classes.empty();
    if (deck == "cdc") return rule.cdcDeck;
    const int order = deckOrder(rule.deck);
    return order != 0 && order <= deckOrder(deck);
}

// CST goal membership: completeness/disclosure and waiver/report integrity
// apply to all goals; clock, crossing and reset contracts apply to the
// analyses they describe. The full table is explicit so an added CST rule
// cannot accidentally disappear from a scoped run.
bool inGoal(std::string_view id, const std::set<std::string>& goals) {
    if (goals.empty()) return true;
    if (id.rfind("LNT-", 0) == 0) return goals.count("lint");
    if (id.rfind("CDC-", 0) == 0) return goals.count("cdc");
    if (id.rfind("RDC-", 0) == 0) {
        if (goals.count("rdc")) return true;
        if (goals.count("cdc")) {
            static const std::set<std::string_view> cdcRdc = {
                "RDC-002", "RDC-003", "RDC-006", "RDC-007", "RDC-016", "RDC-023", "RDC-024"
            };
            return cdcRdc.count(id) != 0;
        }
        return false;
    }
    // Bits: 1 lint, 2 CDC, 4 RDC.
    static const std::map<std::string, unsigned, std::less<>> cst = {
        {"CST-001",6}, // clock and reset classification
        {"CST-002",6}, // constraint object resolution
        {"CST-003",6}, // clock and exception conflicts
        {"CST-004",6}, // crossing clock relationship
        {"CST-005",7}, // black-box inventory
        {"CST-006",7}, // waiver validity
        {"CST-007",6}, // synchronizer trust models
        {"CST-008",6}, // timing exceptions and crossing safety
        {"CST-009",7}, // constraint parser disclosure
        {"CST-010",7}, // unsupported analysis disclosure
        {"CST-011",6}, // sequential clock constraint coverage
        {"CST-012",2}, // IO and IP CDC contracts
        {"CST-013",6}, // generated clock consistency
        {"CST-014",6}, // clock modes and exclusivity
        {"CST-015",7}, // configuration completeness
        {"CST-016",4}, // reset assumptions
        {"CST-017",6}, // black-box protocol and reset metadata
        {"CST-018",7}, // constraint parser completeness
        {"CST-019",7}, // rule coverage and accounting
        {"CST-020",6}, // crossing inventory completeness
        {"CST-021",6}, // clock and reset provenance
        {"CST-022",7}, // waiver hygiene
        {"CST-023",7}, // unmodeled and encrypted coverage
        {"CST-024",6}, // physical CDC/RDC obligations
    };
    auto it = cst.find(id);
    if (it == cst.end()) return true; // unclassified future families remain visible
    unsigned selected = (goals.count("lint") ? 1u : 0u) | (goals.count("cdc") ? 2u : 0u) | (goals.count("rdc") ? 4u : 0u);
    return (it->second & selected) != 0;
}

const RuleInfo* Catalog::find(std::string_view id) const {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &rules_[it->second];
}

std::string ruleHelp(const std::string& id) {
    const auto* rule = Catalog::embedded().find(id);
    if (!rule) return "No offline rule help available.";
    std::ostringstream out;
    std::string specs = reinterpret_cast<const char*>(embedded::specs), heading = "\n### " + id + "\n";
    auto start = specs.find(heading);
    if (start == std::string::npos) out << "No spec block available.\n";
    else { start += heading.size(); auto end = specs.find("\n##", start); out << specs.substr(start, end - start) << "\n"; }
    if (id == "CDC-003") out << R"(
Advice examples (structural checks, not timing proofs):
  Bad: a destination samples an asynchronous source with no recognised synchroniser.
       Review a suitable destination synchroniser and the source stability/pulse contract.
       Declaring the clocks asynchronous does not repair this structure.
  Recognised: a two-stage destination chain with no intervening logic or first-stage fanout.
       Recognition still assumes source stability and metastability resolution; review timing.
  Unknown: no clock relationship is declared. Declare actual clock intent first, then review
       synchronisation; absence of intent is not evidence of safety.
  Waived: retain the accepted owner/reason and review its scope. Waiving does not prove safety.
)";
    return out.str();
}

} // namespace sverity
