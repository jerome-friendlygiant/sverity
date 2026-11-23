// Reset-domain structure checks on the shared model: crossings between reset domains, reset trees built from logic,
// reset synchroniser stage fanout, partially reset state, multiple asynchronous controls, exceptions, polarity and the
// explicit "not modelled" markers (sequencing, power intent, physical recovery/removal).
#include "rules/rdc/rdc_shared.h"
#include "rules/sync_attribute.h"
#include <bit>
#include <deque>
#include <string_view>
#include <unordered_map>
#include "domains/batch_reach.h"

namespace sverity::rdc {

using namespace model;

NodeId RdcShared::assertionSource(NodeId n) const {
    for (int guard = 0; guard < 8; guard++) {
        uint32_t ri = 0;
        const Flop* rf = flopBehind(n, &ri);
        if (!rf) return n;
        SyncInfo si = syncChain(ri);
        if (!si.recognised || si.first == ~0u || m_->flops[si.first].resets.empty()) return n;
        n = m_->flops[si.first].resets[0].root.node;
    }
    return n;
}

std::set<NodeId> RdcShared::asyncSources(const Flop& f) const {
    std::set<NodeId> out;
    for (auto& r : f.resets) out.insert(assertionSource(r.root.node));
    return out;
}

void RdcShared::structure(RuleContext& ctx) {
    for (const char* r : {"RDC-002", "RDC-004", "RDC-005", "RDC-008", "RDC-009", "RDC-010", "RDC-011", "RDC-012", "RDC-013", "RDC-014", "RDC-016",
                          "RDC-017", "RDC-019", "RDC-020", "RDC-025", "RDC-026", "RDC-027", "RDC-028", "RDC-029", "RDC-030"})
        evaluated[r] += 0;
    auto flopName = [&](const Flop& f) { return m_->nameOf(f.q); };
    auto procLoc = [&](const Flop& f) -> const ProcInfo& { return m_->procs[f.proc]; };
    auto isUnreset = [&](const Flop& f) { return f.resets.empty() && !f.hasSyncResetCandidate; };

    // distinct asynchronous reset roots and their users
    std::map<NodeId, std::vector<uint32_t>> usersOf;
    std::set<NodeId> sources;
    for (uint32_t i = 0; i < m_->flops.size(); i++)
        for (auto& r : m_->flops[i].resets) { usersOf[r.root.node].push_back(i); sources.insert(assertionSource(r.root.node)); }
    std::set<NodeId> resetRoots;
    for (auto& [n, u] : usersOf) resetRoots.insert(n);

    // --- registers with an asynchronous reset but no identified clock are skipped by the clock-domain based reset checks below:
    // say so, never leave them without a finding (the clock itself is CDC-001 / CST-004)
    {
        std::set<std::string> seenUnidentified;
        for (auto& f : m_->flops) {
            if (f.clockResolved || f.resets.empty() || !seenUnidentified.insert(stripBit(flopName(f))).second) continue;
            evaluated["RDC-003"]++;
            Diagnostic d = mk("RDC-003", Severity::Warning, "register " + stripBit(flopName(f)) + " has an asynchronous reset but no identified clock: reset deassertion synchronisation and reset-domain crossings "
                              "for it are unresolved", procLoc(f).loc, procLoc(f).instancePath, Confidence::Unresolved);
            d.signals = {stripBit(flopName(f))};
            add(std::move(d));
            ctx.report.markIncomplete("reset-extraction", "register " + stripBit(flopName(f)) + " has a reset but no identified clock");
        }
    }

    // --- RDC-004 / RDC-005: data leaving a register that can be reset asynchronously into one that is not reset by the same source.
    // One finding per destination register and pair of reset sets: sources of that mismatch share a cause and are listed as evidence.
    struct ResetMismatch {
        const Flop* dst = nullptr;
        std::set<NodeId> sourceResets, destinationResets;
        Relation relation = Relation::Same;
        uint32_t launch = 0, capture = 0;
        int launchEdge = 0;
        std::map<std::string, const Flop*> sources;
        uint32_t firstDestination = UINT32_MAX;                              // lowest register index seen with this key
        std::map<std::string, std::pair<std::pair<uint32_t, NodeId>, const Flop*>> firstSource;   // per name: lowest (destination, Q)
    };
    std::map<std::string, ResetMismatch> mismatches;
    // A source whose reset sources are all among the destination's is never read: the cones are walked without such sources.
    std::map<std::set<NodeId>, uint32_t> resetSetIds;
    std::vector<const std::set<NodeId>*> resetSets;
    const auto resetSetId = [&](std::set<NodeId> sources) {
        auto [it, fresh] = resetSetIds.try_emplace(std::move(sources), static_cast<uint32_t>(resetSets.size()));
        if (fresh) resetSets.push_back(&it->first);
        return it->second;
    };
    // Each register's reset set id, computed once (kIgnore: no identified clock or no asynchronous reset).
    std::vector<uint32_t> resetSetOf(m_->flops.size(), SourceClasses::kIgnore);
    for (uint32_t fi = 0; fi < m_->flops.size(); fi++) {
        const Flop& f = m_->flops[fi];
        if (!f.clockResolved) continue;
        auto sources = asyncSources(f);
        if (!sources.empty()) resetSetOf[fi] = resetSetId(std::move(sources));
    }
    const auto resetClasses = cones_->classify([&](uint32_t fi) { return resetSetOf[fi]; }, false);
    // Names are built once per register and reset set: a whole SoC pairs millions of sources with their destinations.
    std::vector<std::string> flopNames(m_->flops.size());
    std::vector<uint8_t> flopNamed(m_->flops.size(), 0);
    const auto strippedName = [&](uint32_t fi) -> const std::string& {
        if (!flopNamed[fi]) { flopNames[fi] = stripBit(m_->nameOf(m_->flops[fi].q)); flopNamed[fi] = 1; }
        return flopNames[fi];
    };
    std::deque<std::string> setNames;   // references stay valid as names are added
    const auto resetSetNames = [&](uint32_t id) -> const std::string& {
        while (setNames.size() <= id) {
            std::string names;
            for (auto n : *resetSets[setNames.size()]) names += stripBit(m_->nameOf(n)) + ",";
            setNames.push_back(std::move(names));
        }
        return setNames[id];
    };
    std::unordered_map<uint64_t, bool> coveredBy;
    const auto covers = [&](uint32_t own, uint32_t set) {   // the reset set `set` is a subset of `own`
        auto [it, fresh] = coveredBy.try_emplace(uint64_t{set} << 32 | own, true);
        if (fresh) it->second = std::includes(resetSets[own]->begin(), resetSets[own]->end(), resetSets[set]->begin(), resetSets[set]->end());
        return it->second;
    };
    std::unordered_map<uint64_t, bool> classCoveredBy;
    const auto classCovered = [&](uint32_t set, uint32_t own) {
        auto [it, fresh] = classCoveredBy.try_emplace(uint64_t{set} << 32 | own, true);
        if (fresh)
            for (const uint32_t cls : resetClasses.classes(set))
                if (!covers(own, cls)) { it->second = false; break; }
        return it->second;
    };
    // Each destination's own reset set; destinations are walked in batches of one reset set, which decides what is pruned.
    std::vector<uint32_t> ownOf(m_->flops.size(), 0), order;
    for (uint32_t di = 0; di < m_->flops.size(); di++) {
        if (!m_->flops[di].clockResolved) continue;
        ownOf[di] = resetSetOf[di] != SourceClasses::kIgnore ? resetSetOf[di] : resetSetId(asyncSources(m_->flops[di]));
        order.push_back(di);
    }
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return ownOf[a] < ownOf[b]; });
    // Mismatches are merged as the batches find them. One cone view per destination, in register order and listing sources by
    // Q, would keep the first destination of each key and the first source of each name: the lowest (destination, Q) is kept.
    {
        BatchWalker walker(*m_);
        std::vector<NodeId> qs;
        // Per batch: the mismatch each (destination, source reset set, launch, capture, edge, relation) maps to.
        std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, int, int>, ResetMismatch*> keyed;
        for (size_t first = 0; first < order.size();) {
            const uint32_t own = ownOf[order[first]];
            size_t last = first;
            while (last < order.size() && last - first < BatchWalker::kLanes && ownOf[order[last]] == own) ++last;
            qs.clear();
            for (size_t i = first; i < last; ++i) qs.push_back(m_->flops[order[i]].q);
            // A node whose reachable reset classes are all covered by the batch's reset set leads to no mismatch.
            walker.run(qs, [&](NodeId n) {
                const uint32_t set = resetClasses.setOf(n);
                return set != SourceClasses::kWide && classCovered(set, own);
            });
            keyed.clear();
            for (const NodeId q : walker.leaves()) {
                const uint32_t si = cones_->flopIndexOf(q);
                if (si == UINT32_MAX) continue;   // a top input
                const uint32_t rsId = resetSetOf[si];
                if (rsId == SourceClasses::kIgnore || covers(own, rsId)) continue;
                const Flop& sf = m_->flops[si];
                const auto& lanes = walker.lanes(q);
                for (size_t w = 0; w < BatchWalker::kWords; ++w)
                    for (uint64_t b = lanes[w]; b; b &= b - 1) {
                        const uint32_t di = order[first + w * 64 + static_cast<size_t>(std::countr_zero(b))];
                        if (di == si) continue;   // a register is not a source of its own cone
                        const Flop& d = m_->flops[di];
                        const auto pair = worstDomains(sf, d, *rel_);
                        const bool sameClock = pair.launch == pair.capture;
                        if (!sameClock && pair.relation == Relation::Same) continue;
                        auto [slot, fresh] = keyed.try_emplace({di, rsId, pair.launch, pair.capture, sf.edge, static_cast<int>(pair.relation)}, nullptr);
                        if (fresh) {
                            const char* rule = sameClock ? "RDC-004" : "RDC-005";
                            const std::string names = ">" + resetSetNames(rsId) + ">" + resetSetNames(own);
                            auto key = std::string(rule) + ":" + strippedName(di) + names + ":" + m_->domains[pair.launch].name + ":" +
                                       std::to_string(sf.edge) + ">" + m_->domains[pair.capture].name + ":" + std::to_string(d.edge) + ":" + relName(pair.relation);
                            auto& mismatch = mismatches[key];
                            if (di < mismatch.firstDestination) {
                                mismatch.firstDestination = di;
                                mismatch.dst = &d;
                                mismatch.sourceResets = *resetSets[rsId];
                                mismatch.destinationResets = asyncSources(d);
                                mismatch.relation = pair.relation;
                                mismatch.launch = pair.launch;
                                mismatch.capture = pair.capture;
                                mismatch.launchEdge = sf.edge;
                            }
                            slot->second = &mismatch;
                        }
                        const std::pair<uint32_t, NodeId> rank{di, q};
                        auto [source, added] = slot->second->firstSource.try_emplace(strippedName(si), rank, &sf);
                        if (!added && rank < source->second.first) source->second = {rank, &sf};
                    }
            }
            first = last;
        }
    }
    for (auto& [key, mismatch] : mismatches) {
        for (const auto& [name, ranked] : mismatch.firstSource) mismatch.sources.emplace(name, ranked.second);
        mismatch.firstSource.clear();
    }
    for (auto& [key, mismatch] : mismatches) {
        const bool sameClock = mismatch.launch == mismatch.capture;
        const char* rule = sameClock ? "RDC-004" : "RDC-005";
        evaluated[rule] += mismatch.sources.size();
        std::string rsn, rdn;
        for (auto n : mismatch.sourceResets) rsn += (rsn.empty() ? "" : ", ") + stripBit(m_->nameOf(n));
        for (auto n : mismatch.destinationResets) rdn += (rdn.empty() ? "" : ", ") + stripBit(m_->nameOf(n));
        const std::string dst = stripBit(m_->nameOf(mismatch.dst->q));
        const std::string first = mismatch.sources.begin()->first;
        const std::string sampled = mismatch.sources.size() == 1 ? first : std::to_string(mismatch.sources.size()) + " registers including " + first;
        Diagnostic x = mk(rule, Severity::Warning,
                          "register " + dst + " samples " + sampled + (mismatch.sources.size() == 1 ? " which is" : " which are") + " asynchronously reset by [" + rsn + "], but is " +
                          (rdn.empty() ? "not reset" : "reset by [" + rdn + "]") + (sameClock ? " (same clock)" : " (different clocks: " + std::string(relName(mismatch.relation)) + ")") +
                          (sameClock ? ": when that reset asserts the source changes asynchronously to the destination clock" :
                                       ": asserting the source reset can change the sampled value without resetting the destination"),
                          procLoc(*mismatch.dst).loc, procLoc(*mismatch.dst).instancePath, Confidence::Heuristic);
        x.identityKey = key;
        x.signals = {dst};
        for (const auto& [name, source] : mismatch.sources) {
            x.signals.push_back(name);
            x.groupedSources.push_back(name);
            x.evidence.push_back({"source_register", name, procLoc(*source).loc});
        }
        x.proofState = ProofState::NotAttempted;
        if (!sameClock) {
            x.message += "; reset compatibility is an unproved functional obligation, even with a data synchroniser";
            x.evidence.push_back({"obligation", "The destination must tolerate the reset-induced value change; metastability synchronisation alone does not establish this.", {}});
        }
        x.nextStep = "Reset both registers by the same reset, or ensure the destination value is don't-care while the source is in reset (documented obligation).";
        add(std::move(x));
    }
    std::set<std::string> done;

    // --- reset trees built from logic: RDC-008 (combined resets), RDC-009 (gated/muxed), RDC-014 (data flop), RDC-017 (select from another reset domain)
    std::set<NodeId> doneRoots;
    for (auto& [root, users] : usersOf) {
        const Flop& u0 = m_->flops[users[0]];
        std::string rname = stripBit(m_->nameOf(root));
        uint32_t gi = 0;
        const Flop* gen = flopBehind(root, &gi);
        if (gen) {
            // a register drives the reset: a recognised synchroniser is RDC-003/006 territory, a data register is a pulse-width risk
            if (syncChain(gi).recognised) continue;
            evaluated["RDC-014"]++;
            Diagnostic d = mk("RDC-014", Severity::Warning, "asynchronous reset " + rname + " is driven by data register " + flopName(*gen) +
                              " (not a reset synchroniser): its pulse width is one generator clock period and it can assert spuriously",
                              procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Heuristic);
            d.signals = {rname};
            d.proofState = ProofState::NotAttempted;
            d.evidence.push_back({"proof_obligation", "minimum reset pulse width and glitch freedom depend on timing/library data and are not analysed", {}});
            add(std::move(d));
            continue;
        }
        if (usersOf.at(root).empty() || m_->nodes[root].sym == nullptr) continue;
        if (!m_->in[root].size()) continue;   // primary input or undriven: nothing to analyse
        std::vector<NodeId> resetLeaves, dataLeaves, undecidedLeaves;
        std::unordered_set<NodeId> seen{root};
        std::vector<NodeId> st{root};
        bool logicSeen = false;
        while (!st.empty()) {
            NodeId n = st.back(); st.pop_back();
            if (n != root && (resetRoots.count(n) || (n < m_->topInput.size() && m_->topInput[n]))) {
                // A leaf is a reset (a reset root, or a primary input the shared classifier takes as one), data, or undecided: a name
                // that may or may not be a reset is not a claim either way.
                const model::ResetClass cls = resetRoots.count(n) ? model::ResetClass{} : model::classifyResetNode(*m_, n);
                const bool isReset = resetRoots.count(n) || cls.reset;
                (isReset ? resetLeaves : cls.undecided ? undecidedLeaves : dataLeaves).push_back(n);
                continue;
            }
            if (m_->flopOfQ.count(n)) {
                uint32_t fi = m_->flopOfQ.at(n);
                (syncChain(fi).recognised ? resetLeaves : dataLeaves).push_back(n);
                continue;
            }
            for (auto ei : m_->in[n]) {
                const Edge& e = m_->edges[ei];
                if (e.type != EdgeType::Comb) continue;
                if (!e.exact) logicSeen = true;
                if (seen.insert(e.from).second) st.push_back(e.from);
            }
        }
        if (!logicSeen || !doneRoots.insert(root).second) continue;
        // RDC-007 for resets built by logic: pick the worst differing user domain (Async > Unknown > Exclusive)
        // so a weaker relationship can never mask a known asynchronous crossing.
        for (auto* leaves : {&resetLeaves, &dataLeaves})
            for (auto n : *leaves) {
                auto fit = m_->flopOfQ.find(n);
                if (fit == m_->flopOfQ.end()) continue;
                const Flop& sf = m_->flops[fit->second];
                if (!sf.clockResolved) continue;
                auto rankOf = [](Relation r) { return r == Relation::Async ? 3 : r == Relation::Unknown ? 2 : r == Relation::Exclusive ? 1 : 0; };
                const Flop* worstUf = nullptr;
                Relation worstRel = Relation::Same;
                for (auto ui : users) {
                    const Flop& uf = m_->flops[ui];
                    if (!uf.clockResolved) continue;
                    Relation rel = rel_->relation(sf.domain, uf.domain);
                    if (rankOf(rel) > rankOf(worstRel)) { worstRel = rel; worstUf = &uf; }
                }
                if (!worstUf) continue;
                evaluated["RDC-007"]++;
                Diagnostic x = mk("RDC-007", worstRel == Relation::Async ? Severity::Error : worstRel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                                  "reset " + rname + " is built by logic from register " + stripBit(m_->nameOf(n)) + " in clock domain '" + m_->domains[sf.domain].name +
                                  "' but resets registers in domain '" + m_->domains[worstUf->domain].name + "' [clock relationship " + relName(worstRel) + "]: its release is asynchronous to the receiving clock",
                                  procLoc(*worstUf).loc, procLoc(*worstUf).instancePath, worstRel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
                x.signals = {rname, stripBit(m_->nameOf(n))};
                x.identityKey = m_->domains[sf.domain].name + ":" + std::to_string(sf.edge) + ">" + m_->domains[worstUf->domain].name + ":" + std::to_string(worstUf->edge) + ":" + relName(worstRel);
                add(std::move(x));
                if (worstRel == Relation::Unknown) ctx.report.markIncomplete("clock-relationships", "reset '" + rname + "' crosses unrelated clocks");
            }
        if (!undecidedLeaves.empty() && dataLeaves.empty()) {
            // Whether a source is a reset decides between a combination of resets (RDC-008) and a reset gated by functional signals
            // (RDC-009): neither is claimed.
            std::string l;
            for (auto n : resetLeaves) l += (l.empty() ? "" : ", ") + stripBit(m_->nameOf(n));
            std::string u;
            for (auto n : undecidedLeaves) u += (u.empty() ? "" : ", ") + stripBit(m_->nameOf(n));
            const std::string why = "whether [" + u + "] is a reset is undecided (not clearly named like one, no asynchronous reset use)";
            for (const char* id : {"RDC-008", "RDC-009"}) {
                if (std::string(id) == "RDC-008" && resetLeaves.size() + undecidedLeaves.size() < 2) continue;
                evaluated[id]++;
                Diagnostic d = mk(id, Severity::Warning, std::string("Unresolved: ") + (std::string(id) == "RDC-008" ? "reset " + rname + " may be a logic combination of resets" +
                                  (l.empty() ? "" : " [" + l + "]") : "reset " + rname + " may be gated or multiplexed by functional signals") + ": " + why,
                                  procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Unresolved);
                d.signals = {rname};
                d.nextStep = "Name the source like a reset, or use it as an asynchronous reset.";
                add(std::move(d));
            }
        } else if (resetLeaves.size() >= 2 && dataLeaves.empty()) {
            evaluated["RDC-008"]++;
            std::string l;
            for (auto n : resetLeaves) l += (l.empty() ? "" : ", ") + stripBit(m_->nameOf(n));
            // How the sources combine: read the operator of the edges into the reset net (Edge::op / inv) and the polarity the users
            // reset on. Sources are assumed to have the net's polarity (flagged in the evidence); only And/Or with true-polarity inputs
            // are classified, anything else stays an unclassified combination.
            bool rootLow = false, polKnown = false;
            for (auto ui : users)
                for (auto& r : m_->flops[ui].resets)
                    if (m_->aliasRoot(r.signal) == root) { rootLow = r.activeLow; polKnown = true; }
            int ands = 0, ors = 0, other = 0;
            std::set<NodeId> srcs;
            for (auto ei : m_->in[root]) {
                const Edge& e = m_->edges[ei];
                if (e.type != EdgeType::Comb) continue;
                srcs.insert(e.from);
                if (e.op == Op::And && e.inv == 0) ands++;
                else if (e.op == Op::Or && e.inv == 0) ors++;
                else other++;
            }
            bool classified = polKnown && srcs.size() >= 2 && other == 0 && (ands == 0) != (ors == 0);
            bool assertsOnAny = classified && ((rootLow && ands) || (!rootLow && ors));
            std::string how = !classified ? "the operators combining them are not classified (not a plain AND/OR of true-polarity sources)"
                            : assertsOnAny ? "it asserts when any source asserts and releases only after the last releases (reconvergent release)"
                                           : "it asserts only while all sources assert and releases as soon as any releases (conditional reset: a partial assertion does not reset)";
            Diagnostic d = mk("RDC-008", Severity::Warning, "reset " + rname + " is a logic combination of " + std::to_string(resetLeaves.size()) + " resets [" + l + "]: " + how,
                              procLoc(u0).loc, procLoc(u0).instancePath, classified ? Confidence::Heuristic : Confidence::Unresolved);
            d.signals = {rname};
            d.evidence.push_back({"combination", !classified ? "unclassified" : assertsOnAny ? "assert-on-any" : "assert-on-all", {}});
            if (classified) d.evidence.push_back({"assumption", std::string("sources have the same polarity as ") + rname + " (" + (rootLow ? "active low" : "active high") + ")", {}});
            add(std::move(d));
        } else if (!dataLeaves.empty()) {
            evaluated["RDC-009"]++;
            std::string l;
            for (auto n : dataLeaves) l += (l.empty() ? "" : ", ") + stripBit(m_->nameOf(n));
            Diagnostic d = mk("RDC-009", Severity::Warning, "reset " + rname + " is gated or multiplexed by functional signal(s) [" + l + "]: combinational reset logic can glitch and assert the reset spuriously",
                              procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Heuristic);
            d.signals = {rname};
            d.nextStep = "Register the gated reset in a flop clocked by the receiving domain, or use a dedicated reset controller.";
            add(std::move(d));
            for (auto n : dataLeaves) {
                auto fit = m_->flopOfQ.find(n);
                if (fit == m_->flopOfQ.end()) continue;
                const Flop& sf = m_->flops[fit->second];
                std::set<NodeId> rs = asyncSources(sf);
                std::set<NodeId> ru;
                for (auto r : users) { auto a = asyncSources(m_->flops[r]); ru.insert(a.begin(), a.end()); }
                if (rs != ru || !sf.clockResolved) {
                    evaluated["RDC-017"]++;
                    Diagnostic x = mk("RDC-017", Severity::Warning, "the select/gate " + stripBit(m_->nameOf(n)) + " of reset " + rname + " comes from a register that is " +
                                      (sf.clockResolved ? "in a different reset domain than the registers the reset controls" : "clocked by an unidentified clock"),
                                      procLoc(sf).loc, procLoc(sf).instancePath, Confidence::Heuristic);
                    x.signals = {rname, stripBit(m_->nameOf(n))};
                    add(std::move(x));
                }
            }
        }
    }

    // --- RDC-030: synchronisation attributes on the stages of a recognised reset synchroniser (same policy as CDC-012)
    {
        std::set<uint32_t> checked;
        for (auto& [root, users] : usersOf) {
            uint32_t last = 0;
            if (!flopBehind(root, &last) || !checked.insert(last).second) continue;
            const SyncInfo si = syncChain(last);
            if (!si.recognised) continue;
            evaluated["RDC-030"]++;
            size_t marked = 0;
            for (auto stage : si.stages) {
                const auto* symbol = m_->nodes[m_->flops[stage].q].sym;
                if (symbol && hasSyncAttribute(*ctx.design.compilation, *symbol)) marked++;
            }
            const bool partial = marked && marked < si.stages.size();
            if (!partial && !(marked == 0 && ctx.config.profile == "fpga")) continue;
            const Flop& stage = m_->flops[last];
            Diagnostic d = mk("RDC-030", Severity::Warning, "reset synchroniser " + stripBit(flopName(stage)) + (partial ?
                              ": only " + std::to_string(marked) + " of " + std::to_string(si.stages.size()) + " stages carry the ASYNC_REG/sverity_sync attribute" :
                              " has no ASYNC_REG attribute on its stages (profile fpga): placement and timing tools may not keep the stages together"),
                              procLoc(stage).loc, procLoc(stage).instancePath, Confidence::Established);
            d.signals = {stripBit(m_->nameOf(root)), stripBit(flopName(stage))};
            d.nextStep = "Mark every stage of the reset synchroniser with (* ASYNC_REG = \"TRUE\" *).";
            add(std::move(d));
        }
    }

    // --- RDC-016: intermediate stages of a recognised reset synchroniser must not feed anything else (one finding per stage)
    std::set<uint32_t> checkedLast;
    std::set<NodeId> reportedStages;
    for (auto& [root, users] : usersOf) {
        uint32_t gi = 0;
        if (!flopBehind(root, &gi)) continue;
        SyncInfo si = syncChain(gi);
        if (!si.recognised || !checkedLast.insert(gi).second) continue;
        for (size_t k = 1; k < si.stages.size(); k++) {
            const Flop& stage = m_->flops[si.stages[k]];
            if (reportedStages.count(stage.q)) continue;
            const Flop& next = m_->flops[si.stages[k - 1]];
            for (auto ei : m_->out[stage.q]) {
                const Edge& e = m_->edges[ei];
                bool toNext = e.type == EdgeType::Seq && e.to == next.q;
                if (!toNext) {
                    reportedStages.insert(stage.q);
                    evaluated["RDC-016"]++;
                    Diagnostic d = mk("RDC-016", Severity::Warning, "intermediate stage " + flopName(stage) + " of the reset synchroniser for " + stripBit(m_->nameOf(root)) +
                                      " also feeds other logic: only the last stage may be used",
                                      procLoc(stage).loc, procLoc(stage).instancePath, Confidence::Established);
                    add(std::move(d));
                    break;
                }
            }
        }
    }

    // --- RDC-019: a reset also used as functional data (SYNCASYNCNET-style mix)
    for (auto& [root, users] : usersOf) {
        bool functional = false;
        for (auto ei : m_->out[root]) {
            const Edge& e = m_->edges[ei];
            if (e.type == EdgeType::AsyncCtl) continue;
            if (e.type == EdgeType::Seq && e.ctrl) { functional = true; break; }
            if (e.type != EdgeType::Comb || !e.exact) { functional = true; break; }
        }
        if (!functional) continue;
        evaluated["RDC-019"]++;
        const Flop& u0 = m_->flops[users[0]];
        Diagnostic d = mk("RDC-019", Severity::Warning, "reset " + stripBit(m_->nameOf(root)) + " asynchronously resets " + std::to_string(users.size()) +
                          " register(s) and is also used as a functional signal (synchronous reset or data): the two uses have different timing requirements",
                          procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Heuristic);
        d.signals = {stripBit(m_->nameOf(root))};
        add(std::move(d));
    }

    // --- RDC-020 / RDC-010: partially reset state
    std::map<std::string, std::vector<uint32_t>> bySym;
    for (uint32_t i = 0; i < m_->flops.size(); i++) bySym[stripBit(m_->nameOf(m_->flops[i].q))].push_back(i);
    for (auto& [name, idx] : bySym) {
        bool anyReset = false, anyPlain = false;
        for (auto i : idx) { anyReset = anyReset || !m_->flops[i].resets.empty(); anyPlain = anyPlain || isUnreset(m_->flops[i]); }
        if (!anyReset || !anyPlain) continue;
        evaluated["RDC-020"]++;
        bool feeds = false;
        for (auto i : idx) if (isUnreset(m_->flops[i])) for (auto ei : m_->out[m_->flops[i].q]) { auto t = m_->edges[ei].type; feeds = feeds || t == EdgeType::Comb || t == EdgeType::Seq; }
        const Flop& f0 = m_->flops[idx[0]];
        Diagnostic d = mk("RDC-020", feeds ? Severity::Warning : Severity::Info, "register " + name + " is only partly reset: some bits have a reset, others keep their value" +
                          (feeds ? " and feed other logic, so that logic sees a mix of reset and stale bits after reset" : ""),
                          procLoc(f0).loc, procLoc(f0).instancePath, Confidence::Heuristic);
        d.signals = {name};
        add(std::move(d));
    }
    done.clear();
    // Only unreset sources of the destination's own instance are read.
    std::unordered_map<std::string, uint32_t> pathIds;
    const auto pathId = [&](const Flop& f) {
        return pathIds.try_emplace(procLoc(f).instancePath, static_cast<uint32_t>(pathIds.size())).first->second;
    };
    const auto unresetClasses = cones_->classify([&](uint32_t fi) {
        const Flop& f = m_->flops[fi];
        return isUnreset(f) ? pathId(f) : SourceClasses::kIgnore;
    }, false);
    // Batched by the destination's instance; per (source, destination) name pair the lowest (destination, source Q) is kept,
    // as one cone view per destination in register order, listing sources by Q, would find it first.
    {
        std::vector<uint32_t> order, ownOf(m_->flops.size(), 0);
        for (uint32_t di = 0; di < m_->flops.size(); di++)
            if (!m_->flops[di].resets.empty()) { ownOf[di] = pathId(m_->flops[di]); order.push_back(di); }
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return ownOf[a] < ownOf[b]; });
        // Per (source name, destination name), as interned name ids: the lowest (destination, source Q, source index).
        std::unordered_map<std::string_view, uint32_t> nameIds;
        std::vector<uint32_t> nameIdOf(m_->flops.size(), UINT32_MAX);
        const auto nameId = [&](uint32_t fi) {
            if (nameIdOf[fi] == UINT32_MAX)
                nameIdOf[fi] = nameIds.try_emplace(strippedName(fi), static_cast<uint32_t>(nameIds.size())).first->second;
            return nameIdOf[fi];
        };
        std::unordered_map<uint64_t, std::tuple<uint32_t, NodeId, uint32_t>> firstPair;
        BatchWalker walker(*m_);
        std::vector<NodeId> qs;
        for (size_t first = 0; first < order.size();) {
            const uint32_t own = ownOf[order[first]];
            size_t last = first;
            while (last < order.size() && last - first < BatchWalker::kLanes && ownOf[order[last]] == own) ++last;
            qs.clear();
            for (size_t i = first; i < last; ++i) qs.push_back(m_->flops[order[i]].q);
            // Nothing behind a node whose unreset registers all sit in other instances is read.
            walker.run(qs, [&](NodeId n) {
                const uint32_t set = unresetClasses.setOf(n);
                if (set == SourceClasses::kWide) return false;
                const auto& classes = unresetClasses.classes(set);
                return !std::binary_search(classes.begin(), classes.end(), own);
            });
            for (const NodeId q : walker.leaves()) {
                const uint32_t si = cones_->flopIndexOf(q);
                if (si == UINT32_MAX) continue;   // a top input
                const Flop& sf = m_->flops[si];
                if (!isUnreset(sf)) continue;
                const auto& lanes = walker.lanes(q);
                for (size_t w = 0; w < BatchWalker::kWords; ++w)
                    for (uint64_t b = lanes[w]; b; b &= b - 1) {
                        const uint32_t di = order[first + w * 64 + static_cast<size_t>(std::countr_zero(b))];
                        if (di == si) continue;   // a register is not a source of its own cone
                        const Flop& d = m_->flops[di];
                        if (procLoc(sf).instancePath != procLoc(d).instancePath) continue;   // unreset register of the same module instance
                        const std::tuple<uint32_t, NodeId, uint32_t> rank{di, q, si};
                        auto [it, fresh] = firstPair.try_emplace(uint64_t{nameId(si)} << 32 | nameId(di), rank);
                        if (!fresh && rank < it->second) it->second = rank;
                    }
            }
            first = last;
        }
        std::vector<std::tuple<uint32_t, NodeId, uint32_t>> kept;
        kept.reserve(firstPair.size());
        for (const auto& [key, rank] : firstPair) kept.push_back(rank);
        std::sort(kept.begin(), kept.end());
        for (const auto& [di, q, si] : kept) {
            const Flop& d = m_->flops[di];
            const Flop& sf = m_->flops[si];
            evaluated["RDC-010"]++;
            Diagnostic x = mk("RDC-010", Severity::Warning, "register " + stripBit(m_->nameOf(sf.q)) + " has no reset and feeds the reset register " +
                              stripBit(m_->nameOf(d.q)) + ": after reset the reset register can capture a stale value", procLoc(sf).loc, procLoc(sf).instancePath, Confidence::Heuristic);
            x.signals = {stripBit(m_->nameOf(sf.q)), stripBit(m_->nameOf(d.q))};
            add(std::move(x));
        }
    }

    // --- RDC-026: more than one asynchronous control on one register. Set + clear is a real conflict (simultaneous assertion has
    // no defined value; the if/else-if order is the priority); several controls forcing the same value are power-on/functional resets.
    // judged per register bit and reported once per register: one bit set by one control and cleared by another is the conflict
    struct Multi { const Flop* first = nullptr; bool conflict = false, unknown = false; };
    std::map<std::string, Multi> multi;
    for (auto& f : m_->flops) {
        if (f.resets.size() < 2) continue;
        auto& mu = multi[stripBit(flopName(f))];
        if (!mu.first) mu.first = &f;
        bool clear = false, set = false;
        for (auto k : f.resetKinds) { clear = clear || k == 0; set = set || k == 1; mu.unknown = mu.unknown || k == 2; }
        mu.conflict = mu.conflict || (clear && set);
    }
    for (auto& [key, mu] : multi) {
        const Flop& f = *mu.first;
        evaluated["RDC-026"]++;
        bool conflict = mu.conflict, unknown = mu.unknown;
        std::string l;
        for (size_t i = 0; i < f.resets.size(); i++)
            l += (l.empty() ? "" : ", ") + stripBit(m_->nameOf(f.resets[i].root.node)) + (f.resets[i].activeLow ? " (negedge" : " (posedge") +
                 (i < f.resetKinds.size() ? (f.resetKinds[i] == 0 ? ", clears)" : f.resetKinds[i] == 1 ? ", sets)" : ")") : ")");
        Diagnostic d = mk("RDC-026", conflict ? Severity::Warning : unknown ? Severity::Warning : Severity::Info,
                          "register " + key + " has " + std::to_string(f.resets.size()) + " asynchronous controls [" + l + "]: " +
                          (conflict ? "a bit is both asynchronously set and cleared, so simultaneous assertion has no defined value (priority = if/else-if order) and releasing them together is a race"
                           : unknown ? "the value each control forces is not constant, so priority and simultaneous assertion cannot be judged"
                                     : "all controls force the same value for every bit (power-on and functional resets); their scenarios are not modelled"),
                          procLoc(f).loc, procLoc(f).instancePath, conflict ? Confidence::Established : unknown ? Confidence::Unresolved : Confidence::Heuristic);
        d.signals = {key};
        if (conflict || unknown) d.proofState = ProofState::NotAttempted;
        add(std::move(d));
        if (unknown) ctx.report.markIncomplete("reset-extraction", "register " + key + " has several asynchronous controls of unknown value");
    }

    // --- RDC-025: primary outputs that depend on registers without any reset
    done.clear();
    for (NodeId n = 0; n < m_->nodes.size(); n++) {
        if (n >= m_->topOutput.size() || !m_->topOutput[n]) continue;
        std::string oname = stripBit(m_->nameOf(n));
        if (done.count(oname)) continue;
        // trace back through combinational logic to registers
        std::vector<NodeId> st{n};
        std::unordered_set<NodeId> seen{n};
        std::string bad;
        const Flop* badFlop = nullptr;
        while (!st.empty() && bad.empty()) {
            NodeId x = st.back(); st.pop_back();
            auto fit = m_->flopOfQ.find(x);
            if (fit != m_->flopOfQ.end()) { if (isUnreset(m_->flops[fit->second])) { bad = stripBit(m_->nameOf(x)); badFlop = &m_->flops[fit->second]; } continue; }
            for (auto ei : m_->in[x]) { const Edge& e = m_->edges[ei]; if (e.type == EdgeType::Comb && seen.insert(e.from).second) st.push_back(e.from); }
        }
        if (bad.empty()) continue;
        done.insert(oname);
        evaluated["RDC-025"]++;
        const ProcInfo& badProc = procLoc(*badFlop);
        Diagnostic d = mk("RDC-025", Severity::Warning, "primary output " + oname + " depends on register " + bad + " which has no reset: the output is undefined while its side of the interface is in reset",
                          badProc.loc, badProc.instancePath, Confidence::Heuristic);
        d.signals = {oname, bad};
        d.evidence.push_back({"capture_register", bad, badProc.loc});
        d.nextStep = "Reset the register, or gate/isolate the output while reset is asserted.";
        add(std::move(d));
    }

    // --- RDC-027: SDC exceptions that start at a reset
    for (auto& ex : ctx.config.constraints.exceptions) {
        if (ex.kind != sdc::PathException::Kind::FalsePath && ex.kind != sdc::PathException::Kind::Multicycle) continue;
        for (auto root : resetRoots) {
            std::string full = stripBit(m_->nameOf(root));
            bool hit = false;
            for (auto& r : ex.from) hit = hit || rel_->nameMatches(r.kind, r.pattern, root, full);
            for (auto& r : ex.through) hit = hit || rel_->nameMatches(r.kind, r.pattern, root, full);
            if (!hit) continue;
            evaluated["RDC-027"]++;
            Diagnostic d = mk("RDC-027", Severity::Warning, std::string(ex.kind == sdc::PathException::Kind::FalsePath ? "set_false_path" : "set_multicycle_path") + " covers reset " + full + ": the exception removes timing checks, it does not make the reset-domain crossing safe",
                              ex.loc, {}, Confidence::Established);
            d.signals = {full};
            add(std::move(d));
        }
    }

    // --- RDC-028: one reset signal used with both polarities; resets derived by logic from another reset
    for (auto& [root, users] : usersOf) {
        bool low = false, high = false;
        for (auto u : users) for (auto& r : m_->flops[u].resets) if (r.root.node == root) (r.activeLow ? low : high) = true;
        if (!(low && high)) continue;
        evaluated["RDC-028"]++;
        const Flop& u0 = m_->flops[users[0]];
        Diagnostic d = mk("RDC-028", Severity::Error, "reset " + stripBit(m_->nameOf(root)) + " is used active-low by some registers and active-high by others",
                          procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Established);
        d.signals = {stripBit(m_->nameOf(root))};
        add(std::move(d));
    }
    for (auto& [root, users] : usersOf) {
        if (m_->nodes[root].sym == nullptr || flopBehind(root)) continue;
        std::vector<NodeId> leaves;
        std::unordered_set<NodeId> seen{root};
        std::vector<NodeId> st{root};
        bool logic = false;
        while (!st.empty()) {
            NodeId n = st.back(); st.pop_back();
            if (n != root && (m_->flopOfQ.count(n) || (n < m_->topInput.size() && m_->topInput[n]))) { leaves.push_back(n); continue; }
            for (auto ei : m_->in[n]) { const Edge& e = m_->edges[ei]; if (e.type == EdgeType::Comb) { logic = logic || !e.exact; if (seen.insert(e.from).second) st.push_back(e.from); } }
        }
        // a reset that is the inverted copy of one source: the polarity changes across the hierarchy (a gated or combined reset is RDC-008/009)
        bool inverted = false, plain = true;
        for (auto ei : m_->in[root]) {
            const Edge& e = m_->edges[ei];
            if (e.type != EdgeType::Comb) continue;
            if (e.inv == 1) inverted = true; else plain = false;
        }
        if (leaves.size() == 1 && logic && inverted && plain) {
            evaluated["RDC-028"]++;
            const Flop& u0 = m_->flops[users[0]];
            Diagnostic d = mk("RDC-028", Severity::Info, "reset " + stripBit(m_->nameOf(root)) + " is the inverted copy of " + stripBit(m_->nameOf(leaves[0])) +
                              ": its polarity differs from its source; verify the receiving blocks assume the inverted polarity", procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Heuristic);
            d.signals = {stripBit(m_->nameOf(root))};
            add(std::move(d));
        }
    }

    // --- RDC-002: reset inputs without a valid input-delay contract (a timing exception alone supplies no reset contract)
    std::map<NodeId, uint32_t> asyncResetInputs;
    for (auto root : resetRoots) {
        if (root < m_->topInput.size() && m_->topInput[root]) {
            asyncResetInputs.emplace(root, usersOf.at(root)[0]);
            continue;
        }
        // Also check primary reset inputs feeding combinational/gated async reset logic (e.g. g = rn & en).
        std::set<NodeId> seen;
        std::vector<NodeId> work{root};
        size_t visits = 0;
        while (!work.empty()) {
            NodeId n = work.back(); work.pop_back();
            if (!seen.insert(n).second || ++visits > 64 || n >= m_->nodes.size()) continue;
            if (n < m_->topInput.size() && m_->topInput[n]) {
                asyncResetInputs.emplace(n, usersOf.at(root)[0]);
                continue;
            }
            if (m_->flopAt(n)) continue;
            if (n >= m_->in.size()) continue;
            for (auto ei : m_->in[n]) {
                const Edge& e = m_->edges[ei];
                if (e.type == EdgeType::Comb) work.push_back(e.from);
            }
        }
    }
    for (auto [input, fi] : asyncResetInputs) {
        std::string full = stripBit(m_->nameOf(input));
        bool rejectedMode = false;
        const auto active = rel_->effectiveInputDelays(input, &rejectedMode);
        if (!rejectedMode && !active.empty()) continue;
        evaluated["RDC-002"]++;
        const Flop& u0 = m_->flops[fi];
        Diagnostic d = mk("RDC-002", Severity::Warning, "reset input " + full + " has no constraint: its timing relation to the clock domains it resets is unknown",
                          procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Unresolved);
        d.signals = {full};
        d.nextStep = "Declare a valid set_input_delay contract for the reset input against its reference clock, and state how release is synchronised.";
        add(std::move(d));
        ctx.report.markIncomplete("reset-extraction", "reset input " + full + " is unconstrained");
    }
    // ... and top-level inputs used as a synchronous reset (the heuristic candidate): its constraint is an input delay,
    // without which whether it is synchronous to the clock it resets is not declared.
    std::map<NodeId, uint32_t> syncResetInputs;   // input -> first register it resets
    const auto topInputLeaves = [&](NodeId start) {
        std::set<NodeId> inputs, seen;
        std::vector<NodeId> work{start};
        size_t visits = 0;
        while (!work.empty()) {
            NodeId node = work.back(); work.pop_back();
            if (!seen.insert(node).second) continue;
            if (++visits > 64 || node >= m_->nodes.size()) break;
            if (node < m_->topInput.size() && m_->topInput[node]) { inputs.insert(node); continue; }
            if (m_->flopAt(node)) continue;
            if (node >= m_->in.size() || m_->in[node].empty()) continue;
            for (auto ei : m_->in[node]) {
                const Edge& edge = m_->edges[ei];
                if (edge.type != EdgeType::Comb || edge.ctrl || edge.approximate ||
                    !(edge.exact || edge.flip || edge.op == Op::And || edge.op == Op::Or || edge.op == Op::Not)) continue;
                work.push_back(edge.from);
            }
        }
        return inputs;
    };
    for (uint32_t fi = 0; fi < m_->flops.size(); ++fi) {
        const Flop& f = m_->flops[fi];
        bool found = false;
        for (const auto& fact : f.syncResetFacts) {
            for (const NodeId r : topInputLeaves(fact.node)) if (!resetRoots.count(r)) {
                syncResetInputs.emplace(r, fi);
                found = true;
            }
        }
        if (!found && f.syncReset != kNoNode && f.syncReset < m_->topInput.size() && m_->topInput[f.syncReset] && !resetRoots.count(f.syncReset))
            syncResetInputs.emplace(f.syncReset, fi);
    }
    for (auto [input, fi] : syncResetInputs) {
        std::string full = stripBit(m_->nameOf(input));
        bool rejectedMode = false;
        const auto active = rel_->effectiveInputDelays(input, &rejectedMode);
        if (!rejectedMode && !active.empty()) continue;
        evaluated["RDC-002"]++;
        const Flop& u0 = m_->flops[fi];
        Diagnostic d = mk("RDC-002", Severity::Warning, "input " + full + " is used as a synchronous reset but has no input delay: whether it is synchronous to the clock it resets, "
                          "or an asynchronous reset that needs synchronising, is not declared", procLoc(u0).loc, procLoc(u0).instancePath, Confidence::Unresolved);
        d.signals = {full};
        d.nextStep = "Declare the input delay of the reset against the clock it resets, or synchronise it if its source is asynchronous.";
        add(std::move(d));
        ctx.report.markIncomplete("reset-extraction", "synchronous reset input " + full + " is unconstrained");
    }

    // --- explicit "not modelled" markers: RDC-011 (sequencing), RDC-012 / RDC-029 (power intent), RDC-013 (recovery/removal)
    if (!sources.empty()) {
        if (sources.size() > 1) {
            evaluated["RDC-011"]++;
            Diagnostic d = mk("RDC-011", Severity::Warning, std::to_string(sources.size()) + " independent asynchronous reset sources exist; the order in which they assert and release is not modelled, "
                              "and no constraint or intent describes it", {}, {}, Confidence::Unresolved);
            d.nextStep = "Document/verify the reset sequencing; this analysis does not prove any ordering.";
            add(std::move(d));
            ctx.report.markIncomplete("reset-extraction", "reset sequencing between " + std::to_string(sources.size()) + " reset sources is not modelled");
        }
        for (const char* id : {"RDC-012"}) {
            evaluated[id]++;
            Diagnostic d = mk(id, Severity::Warning, "no power intent (UPF/CPF) is supported or supplied: retention, isolation and power-domain reset rules are not evaluated",
                              {}, {}, Confidence::Unresolved);
            add(std::move(d));
            ctx.report.markIncomplete("reset-extraction", std::string(id) + ": power intent is not supported");
        }
        for (auto s : sources) {
            evaluated["RDC-013"]++;
            Diagnostic d = mk("RDC-013", Severity::Info, "reset " + stripBit(m_->nameOf(s)) + ": recovery/removal timing, minimum pulse width and metastability are physical properties that "
                              "digital RTL cannot show; they are not analysed", {}, {}, Confidence::Informational);
            d.identityKey = stripBit(m_->nameOf(s));
            d.proofState = ProofState::NotAttempted;
            add(std::move(d));
        }
    }
}

} // namespace sverity::rdc
