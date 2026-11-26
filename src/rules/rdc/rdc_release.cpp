// Release-safe raw resets and release flops (RDC-001 source kind, RDC-002 exemption, RDC-003 INFO).
#include "rules/rdc/rdc_shared.h"

namespace sverity::rdc {
using namespace model;

namespace {
// Follows single-driver exact copies and one-bit inversions back from `n`; `flips` counts the inversions.
NodeId copySource(const Model& m, NodeId n, int& flips) {
    flips = 0;
    for (int hop = 0; hop < 64; ++hop) {
        if (n >= m.in.size() || m.in[n].size() != 1) return n;
        const Edge& e = m.edges[m.in[n][0]];
        if (e.type != EdgeType::Comb || e.ctrl || e.approximate || e.shared || !(e.exact || e.flip)) return n;
        if (e.flip && !e.exact) flips ^= 1;
        n = e.from;
    }
    return n;
}

bool cleanRegister(const Model& m, const Flop& f) {
    return f.clockResolved && !f.blocking && !f.hasIff && f.edge != 3 && !m.procs[f.proc].bodyTiming && f.otherDomains.empty() &&
           !m.multiSequential.count(f.q);
}
} // namespace

bool isShapeSyncStage(const Model& m, const AssertedUses& au, int minStages, uint32_t fi) {
    std::vector<uint32_t> work{fi};
    std::set<uint32_t> seen{fi};
    for (size_t i = 0; i < work.size() && i < 32; ++i) {
        const SyncInfo si = syncChainOf(m, au, work[i]);
        if (si.recognised && si.depth >= minStages && std::find(si.stages.begin(), si.stages.end(), fi) != si.stages.end()) return true;
        if (m.flops[work[i]].q >= m.out.size()) continue;
        for (auto ei : m.out[m.flops[work[i]].q]) {
            const Edge& e = m.edges[ei];
            if (e.type != EdgeType::Seq || e.ctrl || !e.exact) continue;
            const auto it = m.flopOfQ.find(e.to);
            if (it != m.flopOfQ.end() && seen.insert(it->second).second) work.push_back(it->second);
        }
    }
    return false;
}

bool isReleaseFlop(const Model& m, const AssertedUses& au, int minStages, uint32_t fi) {
    const Flop& f = m.flops[fi];
    if (!cleanRegister(m, f) || f.resets.size() != 1 || !f.resetTransitionKnown || f.resetValues.size() != 1) return false;
    const char asserted = f.resetValues[0];
    if (asserted != '0' && asserted != '1') return false;
    const char released = f.releasedSource == kNoNode ? f.releasedValue : m.constantValue(f.releasedSource);
    if (released != (asserted == '0' ? '1' : '0')) return false;
    for (auto ei : m.in[f.q]) {
        const Edge& e = m.edges[ei];
        if (e.type != EdgeType::Seq || m.isDeadEdge(e)) continue;
        if (e.ctrl || !e.exact || m.constantValue(e.from) != released) return false;
    }
    // the reset source: top-level ports, reached through exact copies and recognised synchronisers
    std::vector<std::pair<NodeId, int>> work{{f.resets[0].root.node, 0}};
    std::set<NodeId> seenRoots;
    while (!work.empty()) {
        auto [cur, hop] = work.back();
        work.pop_back();
        if (!seenRoots.insert(cur).second) continue;
        int flips = 0;
        const Root root = m.traceRoot(copySource(m, cur, flips));
        if (root.kind == RootKind::TopPort) continue;
        if (root.kind != RootKind::Logic || hop >= 64) return false;
        uint32_t ri = 0;
        const Flop* rf = m.flopBehind(root.node, &ri);
        if (!rf || ri == fi) return false;
        const SyncInfo si = syncChainOf(m, au, ri);
        if (!si.recognised || si.depth < minStages || si.first == ~0u || m.flops[si.first].resets.empty()) return false;
        for (const auto& r : m.flops[si.first].resets) work.emplace_back(r.root.node, hop + 1);
    }
    // the users: every use of the output is an asynchronous pin of a shape-recognised synchroniser stage
    std::set<uint32_t> users;
    std::vector<NodeId> nodes{f.q};
    std::set<NodeId> seen{f.q};
    for (size_t i = 0; i < nodes.size(); ++i) {
        const NodeId n = nodes[i];
        if ((n < m.topOutput.size() && m.topOutput[n]) || n >= m.out.size()) return false;
        for (auto ei : m.out[n]) {
            const Edge& e = m.edges[ei];
            if (m.isDeadEdge(e) || m.isConstantProc(e.proc)) continue;
            if (e.type == EdgeType::AsyncCtl) {
                const auto it = m.flopOfQ.find(e.to);
                if (it == m.flopOfQ.end()) return false;
                users.insert(it->second);
            } else if (e.type == EdgeType::Comb && !e.ctrl && (e.exact || e.flip)) {
                if (seen.insert(e.to).second) nodes.push_back(e.to);
            } else {
                return false;
            }
        }
    }
    if (users.empty()) return false;
    for (uint32_t ui : users)
        if (!isShapeSyncStage(m, au, minStages, ui)) return false;
    return true;
}

bool RdcShared::releaseSafeGroup(NodeId root, uint32_t dom, bool activeLow, const std::vector<uint32_t>& users) {
    if (users.empty() || root >= m_->nodes.size()) return false;
    if (!evaluator_) evaluator_ = std::make_unique<ReleaseEvaluator>(*m_);
    const int edge = m_->flops[users[0]].edge;
    // every register of this domain and edge asynchronously reset by the root (synchroniser stages included), with its reset value
    std::map<NodeId, char> resetValueOf;
    for (const Flop& f : m_->flops) {
        if (!f.clockResolved || f.domain != dom || f.edge != edge || f.resets.empty()) continue;
        bool byRoot = false;
        for (const auto& r : f.resets) byRoot = byRoot || copyRootOf(r.root.node) == root;
        if (byRoot && f.resets.size() == 1 && f.resetValues.size() == 1 && (f.resetValues[0] == '0' || f.resetValues[0] == '1'))
            resetValueOf[f.q] = f.resetValues[0];
    }
    const uint8_t releasedLevel = activeLow ? 1 : 0;
    std::set<NodeId> tainted;
    std::set<NodeId> late;   // registers whose value after the first edge is not their reset value
    bool secondEdge = false; // the masking pass reads `late` registers as X; the first-edge judgement reads reset values
    const ReleaseEvaluator::Lookup lookup = [&](NodeId n) -> Tri {
        int flips = 0;
        NodeId cur = n;
        for (int hop = 0; hop < 64; ++hop) {
            if (cur == root) return Tri{static_cast<uint8_t>(releasedLevel ^ flips), false};
            if (tainted.count(cur) || (secondEdge && late.count(cur))) return Tri{2, true};
            if (const auto it = resetValueOf.find(cur); it != resetValueOf.end()) return Tri{static_cast<uint8_t>((it->second - '0') ^ flips), false};
            if (cur >= m_->in.size() || m_->in[cur].size() != 1) break;
            const Edge& e = m_->edges[m_->in[cur][0]];
            if (e.type != EdgeType::Comb || e.ctrl || e.approximate || e.shared || !(e.exact || e.flip)) break;
            if (e.flip && !e.exact) flips ^= 1;
            cur = e.from;
        }
        const char c = m_->constantValue(n);
        if (c == '0' || c == '1') return Tri{static_cast<uint8_t>(c - '0'), false};
        return Tri{2, false};
    };
    // (a) stages of recognised synchronisers and (c) release flops are safe by shape; (b) a register whose D is its reset value at release is safe
    // Each register of the group reads, after the first edge, the value its D takes with every other bit at its reset value. Stages and
    // release flops (safe by shape) and registers outside the group list can differ from their reset value there; those read as X.
    {
        const std::set<uint32_t> userSet(users.begin(), users.end());
        std::vector<NodeId> changing;
        for (const auto& [q, rv] : resetValueOf) {
            const auto it = m_->flopOfQ.find(q);
            if (it == m_->flopOfQ.end()) continue;
            const uint32_t fi = it->second;
            if (userSet.count(fi) && !shapeSyncStages_.count(fi) && !isReleaseFlop(*m_, assertedUses_, minStages_, fi)) continue;   // judged below
            bool ok = false;
            const Tri d = evaluator_->nextValue(fi, lookup, ok);
            if (!(ok && d.known() && d.v == static_cast<uint8_t>(rv - '0'))) changing.push_back(q);
        }
        late.insert(changing.begin(), changing.end());
    }
    // a late register that is itself judged is read as X by the evaluation of the others
    std::vector<uint32_t> unsafe;
    for (uint32_t ui : users) {
        const Flop& f = m_->flops[ui];
        if (f.edge != edge) return false;
        if (shapeSyncStages_.count(ui) || isReleaseFlop(*m_, assertedUses_, minStages_, ui)) continue;
        if (!cleanRegister(*m_, f) || f.resets.size() != 1 || f.resetValues.size() != 1 || (f.resetValues[0] != '0' && f.resetValues[0] != '1')) return false;
        if (f.resets[0].activeLow != activeLow) return false;   // one polarity per group: the released level is shared
        bool ok = false;
        const Tri d = evaluator_->nextValue(ui, lookup, ok);
        // a register that holds its reset value and drives an asynchronous pin keeps that reset asserted: it is not a release
        bool drivesAsyncPin = false;
        if (f.q < m_->out.size())
            for (auto ei : m_->out[f.q]) drivesAsyncPin = drivesAsyncPin || m_->edges[ei].type == EdgeType::AsyncCtl;
        if (!(ok && d.known() && d.v == static_cast<uint8_t>(f.resetValues[0] - '0')) || drivesAsyncPin) unsafe.push_back(ui);
    }
    if (!unsafe.empty()) {
        secondEdge = true;
        // the bits that can change at release must be masked at the next edge
        for (uint32_t ui : unsafe) tainted.insert(m_->flops[ui].q);
        if (!clockNodesBuilt_) {
            clockNodes_.insert(clockDomainRoots_.begin(), clockDomainRoots_.end());
            for (const Flop& f : m_->flops) {
                if (f.clockSignal != kNoNode) clockNodes_.insert(f.clockSignal);
                for (NodeId oc : f.otherClockSignals) clockNodes_.insert(oc);
            }
            clockNodesBuilt_ = true;
        }
        const auto& clockNodes = clockNodes_;
        std::vector<NodeId> work(tainted.begin(), tainted.end());
        std::unordered_set<NodeId> reach(tainted.begin(), tainted.end());
        std::set<uint32_t> consumers;
        size_t budget = 2000000;   // nodes the forward reach of U may visit; beyond it the group is not proved safe
        while (!work.empty()) {
            const NodeId n = work.back();
            work.pop_back();
            if (budget-- == 0) return false;
            if ((n < m_->topOutput.size() && m_->topOutput[n]) || clockNodes.count(n) || blackBoxNodes_.count(n)) return false;
            if (n >= m_->out.size()) continue;
            for (auto ei : m_->out[n]) {
                const Edge& e = m_->edges[ei];
                if (m_->isDeadEdge(e) || m_->isConstantProc(e.proc)) continue;
                if (e.type == EdgeType::AsyncCtl) {
                    const auto it = m_->flopOfQ.find(e.to);
                    if (it == m_->flopOfQ.end() || !shapeSyncStages_.count(it->second)) return false;
                } else if (e.type == EdgeType::Comb) {
                    if (reach.insert(e.to).second) work.push_back(e.to);
                } else {
                    const auto it = m_->flopOfQ.find(e.to);
                    if (it == m_->flopOfQ.end()) return false;
                    consumers.insert(it->second);
                }
            }
        }
        // every net downstream of U reads as X at the next edge, whatever drives it
        tainted.insert(reach.begin(), reach.end());
        for (uint32_t gi : consumers) {
            const Flop& g = m_->flops[gi];
            if (!g.clockResolved) return false;
            if (g.domain != dom) {
                if (!isDataSyncFirstStage(gi)) return false;   // another domain is reached only at the first stage of a data synchroniser
                continue;
            }
            if (!cleanRegister(*m_, g)) return false;   // an iff qualifier, timing in the body or several drivers are not evaluated
            bool ok = false;
            const Tri d = evaluator_->nextValue(gi, lookup, ok);
            if (!ok || d.taint) return false;
        }
    }
    for (uint32_t ui : users) releaseSafeFlops_.insert(ui);
    return true;
}

std::pair<NodeId, bool> assertionSourceOf(const Model& m, const AssertedUses& au, NodeId n, bool activeLow) {
    for (int hop = 0; hop <= 64; hop++) {
        int flips = 0;
        n = copySource(m, n, flips);
        activeLow = activeLow ^ (flips != 0);
        uint32_t ri = 0;
        const Flop* rf = m.flopBehind(n, &ri);
        if (!rf) break;
        const SyncInfo si = syncChainOf(m, au, ri);
        if (!si.recognised || si.first == ~0u || m.flops[si.first].resets.empty()) break;
        if (hop == 64) break;
        const auto& r0 = m.flops[si.first].resets[0];
        const char rv = rf->resetValues.empty() ? '?' : rf->resetValues[0];
        if (rv == '0' || rv == '1') {
            const bool syncAssertedLow = rv == '0';
            activeLow = (activeLow == syncAssertedLow) ? r0.activeLow : !r0.activeLow;
        } else {
            activeLow = r0.activeLow;
        }
        n = r0.root.node;
    }
    return {n, activeLow};
}

} // namespace sverity::rdc
