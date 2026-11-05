#include <filesystem>
#include <map>
#include <set>

#include "sverity/rule.h"
#include "testing.h"
using namespace sverity;

TEST(catalog_shape) {
    auto& c = Catalog::embedded();
    std::set<std::string> ids, families{"LNT", "CDC", "RDC", "CST"};
    for (auto& r : c.rules()) {
        CHECK(ids.insert(r.id).second);                       // unique ids
        CHECK(!r.title.empty());
        CHECK(families.count(r.category) == 1);
        CHECK(r.id.rfind(r.category + "-", 0) == 0);
    }
    CHECK(!c.rules().empty());
}

// The registry and the code must agree: a rule the code reports as fully EXECUTED must be marked
// implemented/validated in the catalog, and a PARTIAL rule must not be (it implements a subset).
TEST(catalog_sync_with_implemented_rules) {
    auto& c = Catalog::embedded();
    std::set<std::string> seen;
    for (auto& r : makeAllRules()) {
        std::string id(r->id());
        CHECK(seen.insert(id).second);               // no duplicate registration
        auto* ri = c.find(id);
        CHECK(ri != nullptr);
        if (!ri) continue;
        bool done = ri->status == "implemented" || ri->status == "validated";
        if (r->completeness() == RuleState::Executed) CHECK(done);
        else {
            CHECK(!done);
            if (ri->gap.empty()) { std::printf("  partial rule %s has no 'gap' in the catalog\n", id.c_str()); g_failures++; }
        }
    }
    for (auto& ri : c.rules()) {
        bool done = ri.status == "implemented" || ri.status == "validated";
        if (done) CHECK(seen.count(ri.id) == 1);     // catalog claims implemented -> code must have it
    }
}

// Fixtures are named after the rule they exercise, and a rule that claims to be implemented has at least one
// (waiver/CST rules are covered by the unit tests instead).
TEST(fixtures_match_catalog) {
    namespace fs = std::filesystem;
    auto& c = Catalog::embedded();
    fs::path root = fs::path(SVERITY_SOURCE_DIR) / "tests" / "fixtures";
    std::set<std::string> dirs;
    for (auto& e : fs::directory_iterator(root))
        if (e.is_directory() && e.path().filename().string()[0] != '_') dirs.insert(e.path().filename().string());
    for (auto& d : dirs) {
        if (!c.find(d)) std::printf("  fixture directory '%s' is not a catalog rule\n", d.c_str());
        CHECK(c.find(d) != nullptr);
    }
    for (auto& r : c.rules()) {
        bool done = r.status == "implemented" || r.status == "validated";
        if (done && r.category != "CST" && !dirs.count(r.id)) {
            std::printf("  implemented rule %s has no fixtures\n", r.id.c_str());
            g_failures++;
        }
    }
}

TEST(catalog_every_rule_has_a_deck_or_a_written_exclusion) {
    auto& c = Catalog::embedded();
    size_t core = 0, domain = 0, all = 0;
    for (auto& r : c.rules()) {
        core += inDeck(r, "core"); domain += inDeck(r, "domain"); all += inDeck(r, "1.0");
        CHECK(!inDeck(r, "core") || inDeck(r, "domain"));   // decks are cumulative
        CHECK(!inDeck(r, "domain") || inDeck(r, "1.0"));
    }
    CHECK(core > 0 && core < domain && domain < all);
    CHECK(!inDeck(*c.find("RDC-029"), "1.0"));
    CHECK(!isDeck("nonsense"));
}
