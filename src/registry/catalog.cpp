#include "sverity/catalog.h"

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

const RuleInfo* Catalog::find(std::string_view id) const {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &rules_[it->second];
}

} // namespace sverity
