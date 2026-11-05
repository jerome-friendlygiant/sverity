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
        return cat;
    }();
    return c;
}

static int deckOrder(std::string_view deck) { return deck == "core" ? 1 : deck == "domain" ? 2 : deck == "1.0" ? 3 : 0; }
bool isDeck(std::string_view name) { return deckOrder(name) != 0; }
bool inDeck(const RuleInfo& rule, std::string_view deck) {
    const int order = deckOrder(rule.deck);
    return order != 0 && order <= deckOrder(deck);
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
