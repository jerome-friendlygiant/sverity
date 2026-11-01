#include <map>
#include <set>

#include "sverity/rule.h"
#include "testing.h"
using namespace sverity;

TEST(catalog_shape) {
    auto& c = Catalog::embedded();
    CHECK_EQ(c.rules().size(), 184u);
    std::map<std::string, int> per;
    std::set<std::string> ids;
    for (auto& r : c.rules()) { per[r.category]++; ids.insert(r.id); CHECK(!r.title.empty()); }
    CHECK_EQ(ids.size(), 184u);
    CHECK_EQ(per["LNT"], 61); CHECK_EQ(per["STY"], 16); CHECK_EQ(per["SVA"], 10);
    CHECK_EQ(per["CDC"], 44); CHECK_EQ(per["RDC"], 29); CHECK_EQ(per["CST"], 24);
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
