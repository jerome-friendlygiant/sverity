#include "domains/relations.h"

#include <algorithm>
#include <set>

#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"
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

bool ClockRelations::mayNameMatch(const std::string& kind, const std::string& pattern, NodeId n) const {
    if (kind == "all_inputs" || kind == "all_inputs -no_clocks") return true;
    if (!m_.nodes[n].sym) return false;
    if (pattern.find_first_of("*?") != std::string::npos) return true;
    const std::string_view symbol = m_.nodes[n].sym->name;
    if (symbol.empty() || symbol.back() == ']') return true;   // an escaped name: stripBit can change its end
    // The pattern ends with the symbol's name (`/` read as `.`), before or after one trailing index.
    const auto endsWith = [&](size_t end) {
        if (end < symbol.size()) return false;
        for (size_t i = 0; i < symbol.size(); ++i) {
            const char c = pattern[end - symbol.size() + i];
            if ((c == '/' ? '.' : c) != symbol[i]) return false;
        }
        return true;
    };
    if (endsWith(pattern.size())) return true;
    if (!pattern.empty() && pattern.back() == ']')
        if (const auto open = pattern.rfind('['); open != std::string::npos && endsWith(open)) return true;
    return false;
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

bool ClockRelations::bitMatches(const std::string& kind, const std::string& pattern, NodeId n, std::string_view rtlBitName) const {
    if (!m_.nodes[n].sym) return false;
    const std::string computed = rtlBitName.empty() ? this->rtlBitName(n) : std::string(rtlBitName);
    const std::string full = computed.empty() ? m_.nameOf(n) : computed, p = norm(pattern);
    if (kind == "get_ports") {
        const bool top = n < m_.topInput.size() && (m_.topInput[n] || m_.topOutput[n]);
        const auto bracket = full.rfind('[');
        const std::string local = std::string(m_.nodes[n].sym->name) + (bracket == std::string::npos ? "" : full.substr(bracket));
        return top && (globMatch(p, local) || globMatch(p, full));
    }
    auto dot = full.find('.');
    return globMatch(p, full) || globMatch(p, dot == std::string::npos ? full : full.substr(dot + 1));
}

bool ClockRelations::clockSourceMatches(const sdc::ObjectRef& source, NodeId root, const std::string& full) const {
    if (source.regexp || source.unsupportedOption) return false;
    const auto& kind = source.kind;
    return (kind == "get_ports" || kind == "get_pins" || kind == "get_nets" || kind == "all_inputs") &&
           (nameMatches(kind, source.pattern, root, full) || bitMatches(kind, source.pattern, root, rtlBitName(root)));
}

std::string ClockRelations::sdcClockForNode(NodeId n, bool* multipleClocks) const {
    if (n == kNoNode) return {};
    std::string full = stripBit(m_.nameOf(n));
    std::set<std::string> seen;
    std::set<const sdc::ClockDef*> active;
    for (auto it = cfg_.constraints.clocks.rbegin(); it != cfg_.constraints.clocks.rend(); ++it)
        if (seen.insert(it->name).second) active.insert(&*it);
    std::string firstName;
    for (const auto& c : cfg_.constraints.clocks) {
        if (!active.count(&c)) continue; // the latest definition owns this name
        for (auto& s : c.sources)
            if (clockSourceMatches(s, n, full)) {
                const auto& name = c.name.empty() ? s.pattern : c.name;
                if (!multipleClocks) return name;
                if (firstName.empty()) firstName = name;
                else if (name != firstName) {
                    *multipleClocks = true;
                    return firstName;
                }
                break;
            }
    }
    return firstName;
}

ClockRelations::ClockRelations(const Model& m, const Config& cfg) : m_(m), cfg_(cfg) {
    names_.assign(m.domains.size(), {});
    multipleClocks_.resize(m.domains.size());
    for (size_t i = 0; i < m.domains.size(); i++) {
        bool multiple = false;
        names_[i] = sdcClockForNode(m.domains[i].root, &multiple);
        multipleClocks_[i] = multiple;
    }
    const auto& delays = cfg_.constraints.inputDelays;
    const auto& rejectedEvents = cfg_.constraints.rejectedInputDelayEvents;
    if (!delays.empty() || !rejectedEvents.empty()) {
        std::vector<NodeId> inputs;
        for (NodeId n = 0; n < m_.topInput.size(); ++n)
            if (m_.topInput[n]) inputs.push_back(n);
        const auto& clocks = cfg_.constraints.clocks;
        size_t evIdx = 0;
        auto emitRejectedBefore = [&](size_t idx) {
            while (evIdx < rejectedEvents.size() && rejectedEvents[evIdx].afterInputDelayIndex <= idx) {
                inputContracts_.push_back({nullptr, false, true});
                ++evIdx;
            }
        };
        for (size_t i = 0; i < delays.size(); ++i) {
            emitRejectedBefore(i);
            const auto& d = delays[i];
            const bool hasUnboundedSelector = d.ports.empty() ||
                std::any_of(d.ports.begin(), d.ports.end(), [&](const auto& ref) {
                    const auto kind = ref.kind.empty() ? "get_ports" : ref.kind;
                    return ref.regexp || ref.unsupportedOption || (kind != "get_ports" && kind != "all_inputs" && kind != "all_inputs -no_clocks");
                });
            const bool portsValid = !hasUnboundedSelector && std::all_of(d.ports.begin(), d.ports.end(), [&](const auto& ref) {
                return std::any_of(inputs.begin(), inputs.end(), [&](NodeId n) {
                    return inputDelayMatches(ref, n);
                });
            });
            const bool clockValid = std::any_of(clocks.begin(), clocks.end(), [&](const auto& ck) {
                return (ck.name.empty() && !ck.sources.empty() ? ck.sources[0].pattern : ck.name) == d.clock;
            });
            inputContracts_.push_back({&d, portsValid && clockValid, hasUnboundedSelector});
        }
        emitRejectedBefore(delays.size());
    }
}

std::string ClockRelations::rtlBitName(NodeId nodeId) const {
    if (nodeId >= m_.nodes.size()) return {};
    const auto& node = m_.nodes[nodeId];
    if (!node.sym || m_.collapsed(nodeId)) return {};
    const auto& type = node.sym->getType();
    if (type.isScalar()) return m_.nameOf(nodeId);
    if (!type.isSimpleBitVector()) return {};
    const auto range = type.getFixedRange();
    const auto index = static_cast<int64_t>(range.right) +
        (range.isDescending() ? static_cast<int64_t>(node.bit) : -static_cast<int64_t>(node.bit));
    return stripBit(m_.nameOf(nodeId)) + "[" + std::to_string(index) + "]";
}

std::string ClockRelations::boundaryInputName(NodeId input) const {
    return rtlBitName(input);
}

bool ClockRelations::inputDelayMatches(const sdc::ObjectRef& ref, NodeId node) const {
    if (ref.regexp || ref.unsupportedOption) return false;
    const auto kind = ref.kind.empty() ? "get_ports" : ref.kind;
    if (kind != "get_ports" && kind != "all_inputs" && kind != "all_inputs -no_clocks") return false;
    if (nameMatches(kind, ref.pattern, node, stripBit(m_.nameOf(node)))) return true;
    const auto bitName = boundaryInputName(node);
    return !bitName.empty() && bitMatches(kind, ref.pattern, node, bitName);
}

std::vector<ClockRelations::ActiveInputContract> ClockRelations::effectiveInputDelays(
    NodeId node, bool* rejectedMode) const {
    std::vector<ActiveInputContract> active;
    unsigned uncertainModes = 0;
    for (const auto& declaration : inputContracts_) {
        if (!declaration.delay) {
            uncertainModes |= 3u;
            continue;
        }
        const auto& delay = *declaration.delay;
        const bool matches = declaration.unboundedTargets ||
            std::any_of(delay.ports.begin(), delay.ports.end(), [&](const auto& ref) {
                return inputDelayMatches(ref, node);
            });
        if (!matches) continue;
        const unsigned modes = delay.min && !delay.max ? 1u : delay.max && !delay.min ? 2u : 3u;
        if (!declaration.valid) {
            uncertainModes |= modes;
            continue;
        }
        if (!delay.addDelay) {
            uncertainModes &= ~modes;
            for (auto it = active.begin(); it != active.end();) {
                it->modes &= ~modes;
                if (!it->modes) it = active.erase(it); else ++it;
            }
        }
        active.push_back({&delay, modes});
    }
    if (rejectedMode) *rejectedMode = uncertainModes != 0;
    return active;
}

Relation ClockRelations::relation(uint32_t a, uint32_t b) const {
    const size_t count = m_.domains.size();
    if (a >= count || b >= count) return computeRelation(a, b);
    // A table of every pair while it stays small (4 MiB at 2048 domains); past that, only the pairs asked about.
    if (count <= kDenseRelationDomains) {
        if (relationOf_.empty()) relationOf_.assign(count * count, -1);
        int8_t& known = relationOf_[size_t{a} * count + b];
        if (known < 0) known = static_cast<int8_t>(computeRelation(a, b));
        return static_cast<Relation>(known);
    }
    auto [it, fresh] = sparseRelationOf_.try_emplace(uint64_t{a} << 32 | b, Relation::Unknown);
    if (fresh) it->second = computeRelation(a, b);
    return it->second;
}

Relation ClockRelations::computeRelation(uint32_t a, uint32_t b) const {
    // A domain rooted on a port with several active clock declarations is not
    // equivalent to itself for constraint reasoning: the root may be running
    // under any of those clocks. In particular, do this before the identity
    // shortcut so declaration order cannot turn an ambiguous root into Same.
    if (hasMultipleClocks(a) || hasMultipleClocks(b)) return Relation::Unknown;
    if (a == b) return Relation::Same;
    // Distinct modeled clock roots sharing one SDC clock name are not proven equivalent
    // (same RTL alias root already resolves to a == b).
    if (!names_[a].empty() && names_[a] == names_[b]) return Relation::Unknown;
    return relationByNames(names_[a], names_[b]);
}

Relation ClockRelations::relationByNames(const std::string& ca, const std::string& cb, bool requireConsistent) const {
    if (!ca.empty() && ca == cb) return Relation::Same;
    if (ca.empty() || cb.empty()) return Relation::Unknown;
    auto inGroup = [&](const sdc::ClockGroups& g, const std::string& c) -> int {
        int match = -1;
        for (size_t i = 0; i < g.groups.size(); i++)
            for (auto& o : g.groups[i]) {
                if (o.regexp || o.unsupportedOption) continue;
                if (!o.kind.empty() && o.kind != "get_clocks" && o.kind != "all_clocks") continue;
                if (o.kind == "all_clocks" || globMatch(o.pattern, c)) {
                    if (!requireConsistent) return static_cast<int>(i);
                    if (match >= 0 && match != static_cast<int>(i)) return -2;
                    match = static_cast<int>(i);
                }
            }
        return match;
    };
    bool found = false;
    Relation resolved = Relation::Unknown;
    for (auto& g : cfg_.constraints.clockGroups) {
        int ia = inGroup(g, ca), ib = inGroup(g, cb);
        if (ia == -2 || ib == -2) return Relation::Unknown;
        if (ia < 0 || ib < 0 || ia == ib) continue;
        const auto current = g.kind == sdc::ClockGroups::Kind::Asynchronous ? Relation::Async
                           : g.kind == sdc::ClockGroups::Kind::Unspecified ? Relation::Unknown : Relation::Exclusive;
        if (!requireConsistent) return current;
        if (found && resolved != current) return Relation::Unknown;
        found = true;
        resolved = current;
    }
    return resolved;
}

bool ClockRelations::hasClockGroupCut(uint32_t a, uint32_t b) const {
    for (const auto& g : cfg_.constraints.clockGroups)
        for (const auto& group : g.groups)
            for (const auto& ref : group) {
                if (!ref.kind.empty() && ref.kind != "get_clocks" && ref.kind != "all_clocks") continue;
                if (ref.regexp || ref.unsupportedOption) return true;
            }
    if (a >= names_.size() || b >= names_.size()) return false;
    const auto& ca = names_[a];
    const auto& cb = names_[b];
    if (ca.empty() || cb.empty()) return false;
    auto inGroup = [&](const sdc::ClockGroups& g, const std::string& c) -> int {
        int match = -1;
        for (size_t i = 0; i < g.groups.size(); ++i)
            for (const auto& o : g.groups[i]) {
                if (!o.kind.empty() && o.kind != "get_clocks" && o.kind != "all_clocks") continue;
                if (o.kind == "all_clocks" || globMatch(o.pattern, c)) {
                    if (match >= 0 && match != static_cast<int>(i)) return -2;
                    match = static_cast<int>(i);
                }
            }
        return match;
    };
    for (const auto& g : cfg_.constraints.clockGroups) {
        const int ia = inGroup(g, ca), ib = inGroup(g, cb);
        if (ia == -2 || ib == -2) return true;
        if (ia >= 0 && ib >= 0 && ia != ib) return true;
    }
    return false;
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
        return {launch.domain, capture.domain, relations.relation(launch.domain, capture.domain)};
    DomainPair worst{launch.domain, capture.domain, Relation::Same};
    int worstRisk = -1;
    for (auto a : domainsOf(launch))
        for (auto b : domainsOf(capture)) {
            const Relation r = relations.relation(a, b);
            if (risk(r) > worstRisk) { worstRisk = risk(r); worst = {a, b, r}; }
        }
    return worst;
}

} // namespace sverity::model
