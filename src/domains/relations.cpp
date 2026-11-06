#include "domains/relations.h"

#include <algorithm>
#include <set>

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
    if (!m_.nodes[n].sym) return false;   // a logic hub is no design object
    std::string p = norm(pattern);
    if (kind == "all_inputs") return n < m_.topInput.size() && m_.topInput[n];
    // Clocks of the whole constraint set, whatever their order: leaving a port out gives it no contract, which is the cautious side.
    if (kind == "all_inputs -no_clocks") return n < m_.topInput.size() && m_.topInput[n] && !isDeclaredClock(n);
    if (kind == "get_ports") {
        bool top = n < m_.topInput.size() && (m_.topInput[n] || m_.topOutput[n]);
        return top && (globMatch(p, std::string(m_.nodes[n].sym->name)) || globMatch(p, full));
    }
    auto dot = full.find('.');
    std::string rel = dot == std::string::npos ? full : full.substr(dot + 1);
    return globMatch(p, full) || globMatch(p, rel);
}

bool ClockRelations::isDeclaredClock(NodeId n) const {
    if (!sdcClockForNode(n).empty()) return true;
    std::set<std::string> seen;
    for (auto it = cfg_.constraints.clocks.rbegin(); it != cfg_.constraints.clocks.rend(); ++it) {
        const auto& clock = *it;
        if (!seen.insert(clock.name).second) continue;
        for (const auto& source : clock.sources)
            if ((source.kind == "get_ports" || source.kind == "get_pins" || source.kind == "get_nets") && bitMatches(source.kind, source.pattern, n)) return true;
    }
    return false;
}

bool ClockRelations::bitMatches(const std::string& kind, const std::string& pattern, NodeId n) const {
    if (!m_.nodes[n].sym) return false;
    const std::string full = m_.nameOf(n), p = norm(pattern);
    if (kind == "get_ports") {
        const bool top = n < m_.topInput.size() && (m_.topInput[n] || m_.topOutput[n]);
        const auto bracket = full.rfind('[');
        const std::string local = std::string(m_.nodes[n].sym->name) + (bracket == std::string::npos ? "" : full.substr(bracket));
        return top && (globMatch(p, local) || globMatch(p, full));
    }
    auto dot = full.find('.');
    return globMatch(p, full) || globMatch(p, dot == std::string::npos ? full : full.substr(dot + 1));
}

std::string ClockRelations::sdcClockForNode(NodeId n) const {
    if (n == kNoNode) return {};
    std::string full = stripBit(m_.nameOf(n));
    std::set<std::string> seen;
    std::set<const sdc::ClockDef*> active;
    for (auto it = cfg_.constraints.clocks.rbegin(); it != cfg_.constraints.clocks.rend(); ++it)
        if (seen.insert(it->name).second) active.insert(&*it);
    for (const auto& c : cfg_.constraints.clocks) {
        if (!active.count(&c)) continue; // the latest definition owns this name
        for (auto& s : c.sources)
            if ((s.kind == "get_ports" || s.kind == "get_pins" || s.kind == "get_nets" || s.kind == "all_inputs") &&
                nameMatches(s.kind, s.pattern, n, full))
                return c.name.empty() ? s.pattern : c.name;
    }
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

std::vector<uint32_t> domainsOf(const Flop& f) {
    std::vector<uint32_t> all{f.domain};
    all.insert(all.end(), f.otherDomains.begin(), f.otherDomains.end());
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    return all;
}

DomainPair worstDomains(const Flop& launch, const Flop& capture, const ClockRelations& relations) {
    const auto risk = [](Relation r) { return r == Relation::Async ? 3 : r == Relation::Unknown ? 2 : r == Relation::Exclusive ? 1 : 0; };
    if (launch.otherDomains.empty() && capture.otherDomains.empty())
        return {launch.domain, capture.domain, launch.domain == capture.domain ? Relation::Same : relations.relation(launch.domain, capture.domain)};
    DomainPair worst{launch.domain, capture.domain, Relation::Same};
    int worstRisk = -1;
    for (auto a : domainsOf(launch))
        for (auto b : domainsOf(capture)) {
            const Relation r = a == b ? Relation::Same : relations.relation(a, b);
            if (risk(r) > worstRisk) { worstRisk = risk(r); worst = {a, b, r}; }
        }
    return worst;
}

} // namespace sverity::model
