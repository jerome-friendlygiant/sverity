#include "domains/relations.h"

#include <algorithm>

#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "sverity/waiver.h"   // globMatch

namespace sverity::model {

const char* relName(Relation r) {
    switch (r) {
        case Relation::Same: return "synchronous (same clock)";
        case Relation::Async: return "declared asynchronous";
        case Relation::Exclusive: return "declared exclusive";
        case Relation::Unknown: return "UNKNOWN (no constraint relates them)";
    }
    return "?";
}

std::string stripBit(std::string s) {
    auto p = s.rfind('[');
    if (p != std::string::npos && !s.empty() && s.back() == ']') s.resize(p);
    return s;
}

static std::string norm(std::string p) {
    std::replace(p.begin(), p.end(), '/', '.');
    return p;
}

bool ClockRelations::nameMatches(const std::string& kind, const std::string& pattern, NodeId n,
                                 const std::string& full) const {
    std::string p = norm(pattern);
    if (kind == "all_inputs") return n < m_.topInput.size() && m_.topInput[n];
    if (kind == "get_ports") {
        bool top = n < m_.topInput.size() && (m_.topInput[n] || m_.topOutput[n]);
        return top && (globMatch(p, std::string(m_.nodes[n].sym->name)) || globMatch(p, full));
    }
    auto dot = full.find('.');
    std::string rel = dot == std::string::npos ? full : full.substr(dot + 1);
    return globMatch(p, full) || globMatch(p, rel);
}

std::string ClockRelations::sdcClockForNode(NodeId n) const {
    if (n == kNoNode) return {};
    std::string full = stripBit(m_.nameOf(n));
    for (auto& c : cfg_.constraints.clocks)
        for (auto& s : c.sources)
            if ((s.kind == "get_ports" || s.kind == "get_pins" || s.kind == "get_nets" || s.kind == "all_inputs") &&
                nameMatches(s.kind, s.pattern, n, full))
                return c.name.empty() ? s.pattern : c.name;
    return {};
}

ClockRelations::ClockRelations(const Model& m, const Config& cfg) : m_(m), cfg_(cfg) {
    names_.assign(m.domains.size(), {});
    for (size_t i = 0; i < m.domains.size(); i++) names_[i] = sdcClockForNode(m.domains[i].root);
}

Relation ClockRelations::relation(uint32_t a, uint32_t b) const {
    if (a == b) return Relation::Same;
    return relationByNames(names_[a], names_[b]);
}

Relation ClockRelations::relationByNames(const std::string& ca, const std::string& cb) const {
    if (!ca.empty() && ca == cb) return Relation::Same;
    if (ca.empty() || cb.empty()) return Relation::Unknown;
    auto inGroup = [&](const sdc::ClockGroups& g, const std::string& c) -> int {
        for (size_t i = 0; i < g.groups.size(); i++)
            for (auto& o : g.groups[i])
                if (globMatch(o.pattern, c)) return static_cast<int>(i);
        return -1;
    };
    for (auto& g : cfg_.constraints.clockGroups) {
        int ia = inGroup(g, ca), ib = inGroup(g, cb);
        if (ia >= 0 && ib >= 0 && ia != ib)
            return g.kind == sdc::ClockGroups::Kind::Asynchronous ? Relation::Async
                 : g.kind == sdc::ClockGroups::Kind::Unspecified ? Relation::Unknown : Relation::Exclusive;
    }
    return Relation::Unknown;
}

} // namespace sverity::model
