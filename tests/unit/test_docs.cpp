// docs/rules/<FAMILY>.md tables must list exactly the catalog's rules with identical titles.
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

#include "sverity/catalog.h"
#include "testing.h"
using namespace sverity;

TEST(rule_docs_match_catalog) {
    std::map<std::string, std::string> docs;
    for (const char* fam : {"LNT", "STY", "SVA", "CDC", "RDC", "CST"}) {
        std::ifstream f(std::string(SVERITY_SOURCE_DIR) + "/docs/rules/" + fam + ".md");
        CHECK(f.good());
        std::string line;
        std::regex row("^\\| `([A-Z]{3}-[0-9]{3})` \\| (.*) \\|$");
        while (std::getline(f, line)) {
            std::smatch m;
            if (std::regex_match(line, m, row)) CHECK(docs.emplace(m[1], m[2]).second);   // no duplicate rows
        }
    }
    auto& cat = Catalog::embedded();
    CHECK_EQ(docs.size(), cat.rules().size());
    for (auto& r : cat.rules()) {
        auto it = docs.find(r.id);
        CHECK(it != docs.end());
        if (it != docs.end()) CHECK_EQ(it->second, r.title);
    }
}
