#pragma once
// Reset-domain analysis on the shared model: where each asynchronous reset comes from, whether its release is
// synchronised to the clock of the registers it resets, and whether it is generated in another clock domain.
//
// Policy (same as CDC): a reset whose release cannot be shown to be synchronised is a WARNING (unresolved), a reset
// generated in a clock domain declared asynchronous to its users is an ERROR, a recognised reset synchroniser is INFO
// with its trust model. Nothing is reported as safe.
#include <algorithm>
#include <memory>
#include <map>
#include <set>

#include "domains/cone.h"
#include "domains/reset_contracts_bind.h"
#include "domains/model.h"
#include "domains/relations.h"
#include "rules/rdc/rdc_eval.h"
#include "rules/rules.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"

namespace sverity {
namespace rdc {

using namespace model;

struct SyncInfo {
    int depth = 0;
    bool recognised = false;   // exact NBA assertion/release transitions, common reset polarity and clock edge
    std::string why;           // reason when not recognised
    bool logic = false;        // not recognised because a stage's data input is logic / several sources
    std::vector<uint32_t> stages;   // flop indices, last stage first
    uint32_t first = ~0u;      // first stage (constant D) when recognised
};

/// Whether an asynchronous reset of this register bit really resets it. A reset entry whose branch holds the bit (`'n'`: the branch
/// does not assign it; `'h'`: it assigns its own value) forces nothing, so a bit with only such entries is unreset for RDC-010/020/025.
/// The async-control rules (RDC-012/013/026) keep reading the raw reset list.
inline bool resetsBit(const model::Flop& f) {
    if (f.resets.empty()) return false;
    if (f.resetValues.size() != f.resets.size()) return true;
    for (char v : f.resetValues) if (v != 'n' && v != 'h') return true;
    return false;
}

// A bit with an asynchronous control whose forced value this analysis could not decide: a non-constant or unanalysed value ('?'),
// or an X or Z, which is not a binary reset. It is neither a known reset nor held, so the rules that depend on that distinction
// report Unresolved for it.
inline bool undecidedBit(const model::Flop& f) {
    for (char v : f.resetValues) if (v == '?' || v == 'x' || v == 'z') return true;
    return false;
}

/// If `rootNode` is driven purely by a binary constant (literal, constant wire, localparam/parameter, or port tied
/// to a constant, optionally through exact/inverted copies), returns its constant bit value ('0' or '1'); else '?'.
inline char tiedResetValue(const model::Model& m, model::NodeId rootNode) {
    if (rootNode == model::kNoNode) return '?';
    bool flipped = false;
    model::NodeId cur = rootNode;
    for (int hop = 0; hop < 16; ++hop) {
        const char cv = m.constantValue(cur);
        if (cv == '0' || cv == '1') return flipped ? (cv == '0' ? '1' : '0') : cv;
        if (cur >= m.in.size() || m.in[cur].size() != 1) break;
        const model::Edge& e = m.edges[m.in[cur][0]];
        if (e.type != model::EdgeType::Comb || e.ctrl || e.approximate || e.shared || !(e.exact || e.flip)) break;
        if (e.flip) flipped = !flipped;
        cur = e.from;
    }
    return '?';
}

/// Asserted levels that asynchronous resets use, per register that drives the reset (rdc_structure.cpp).
using AssertedUses = std::map<uint32_t, std::set<char>>;
AssertedUses assertedUsesOf(const model::Model& m);
/// Walks a candidate reset synchroniser from its last stage back to the constant-D first stage (rdc_structure.cpp).
SyncInfo syncChainOf(const model::Model& m, const AssertedUses& assertedUses, uint32_t last);
/// Whether register `fi` is a release flop: forced asynchronously to the asserted level by a top-level port or recognised synchroniser source, its D the
/// released constant, with every asynchronous-reset user a stage of a reset synchroniser recognised by chain shape (any clock domain) and nothing else
/// reading it (rdc_release.cpp).
bool isReleaseFlop(const model::Model& m, const AssertedUses& assertedUses, int minStages, uint32_t fi);
/// Whether register `fi` is a stage of a reset synchroniser recognised by chain shape with at least `minStages` stages (rdc_release.cpp).
bool isShapeSyncStage(const model::Model& m, const AssertedUses& assertedUses, int minStages, uint32_t fi);
/// The assertion source of an asynchronous reset node and its effective active-low polarity, traced through exact/inverted copies and
/// recognised reset synchronisers (rdc_release.cpp).
std::pair<model::NodeId, bool> assertionSourceOf(const model::Model& m, const AssertedUses& assertedUses, model::NodeId n, bool activeLow);
/// The reset_contracts binding of the design: pure, computed once per run by RdcShared and by the CDC protocol rules (rdc_structure.cpp).
model::ContractBindingResult bindContracts(const model::Model& m, const ClockRelations& rel, const Config& config, const AssertedUses& uses);

/// One clause cited as evidence on a finding. Kind site_signal is excluded from finding identity, grouping and baseline keys, so a
/// cited contract never changes the fingerprint, count or severity of the finding it annotates.
inline void citeClause(Diagnostic& d, const model::ContractBinding& c) {
    d.evidence.push_back({"site_signal", c.id + " (" + model::contractKindName(c.kind) + ", " + model::contractStatusName(c.status) + ")", {}});
}
/// Release orders between two sets of exact reset roots: each clause cited with its status, and an Established order adds its scope.
inline void citeReleaseOrders(Diagnostic& d, const model::ContractBindingResult& result, const std::set<std::string>& rootsA,
                              const std::set<std::string>& rootsB) {
    std::set<std::string> cited;
    for (const auto& a : rootsA)
        for (const auto& b : rootsB) {
            if (a == b) continue;
            for (const auto* c : model::ordersBetween(result, a, b)) {
                if (!cited.insert(c->id).second) continue;
                citeClause(d, *c);
                if (c->status == model::ContractStatus::Established)
                    d.evidence.push_back({"site_signal", "release order established structurally under the stated initial state; protocol quiescence and reset-state correctness remain open", {}});
            }
        }
}

class RdcShared {
public:
    std::map<std::string, std::vector<Diagnostic>> findings;
    std::map<std::string, uint64_t> evaluated;
    bool built = false;

    void ensure(RuleContext& ctx) {
        if (built) return;
        built = true;
        m_ = &getModel(ctx.design);
        rel_ = std::make_unique<ClockRelations>(*m_, ctx.config);
        ctx.report.declareScope("reset-extraction");
        int minStages = 2;
        {
            std::string value = ctx.config.option("RDC-006.min_stages", "2");
            size_t end = 0;
            minStages = std::stoi(value, &end);
            if (end != value.size() || minStages < 2) throw std::invalid_argument("RDC-006.min_stages must be an integer >= 2");
        }
        minStages_ = minStages;
        cdcMinStages_ = 2;
        try {
            const std::string value = ctx.config.option("CDC-005.min_stages", "2");
            size_t end = 0;
            const int parsed = std::stoi(value, &end);
            if (end == value.size() && parsed >= 2) cdcMinStages_ = parsed;
        } catch (...) {}
        assertedUses_ = assertedUsesOf(*m_);
        contracts_ = bindContracts(*m_, *rel_, ctx.config, assertedUses_);
        cones_ = &sharedCones(getModel(ctx.design));
        blackBoxNodes_.clear();
        for (const auto& bb : m_->blackBoxes)
            for (const auto& port : bb.ports)
                for (NodeId n : port.nodes)
                    if (n != kNoNode) blackBoxNodes_.insert(n);
        clockDomainRoots_.clear();
        for (const auto& dm : m_->domains)
            if (dm.root != kNoNode) clockDomainRoots_.insert(dm.root);
        dataSyncLastMemo_.assign(m_->flops.size(), -2);
        analyse(ctx, minStages);
        structure(ctx);
        resetContracts(ctx);
    }
    void flush(const std::string& rule, RuleContext& ctx) {
        for (auto& d : findings[rule]) ctx.report.add(d);
        // a marker belongs to the rule that raised it: it applies only when that rule runs
        if (const auto it = pendingMarks_.find(rule); it != pendingMarks_.end())
            for (const auto& [scope, reason] : it->second) ctx.report.markIncomplete(scope, reason);
        ctx.objectsEvaluated = evaluated[rule];
    }

private:
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> pendingMarks_;   // rule -> scope markers applied by flush
    int minStages_ = 2;
    int cdcMinStages_ = 2;
    std::set<uint32_t> shapeSyncStages_;               // stages of reset synchronisers recognised by chain shape, whatever their consumer
    std::unordered_set<NodeId> clockNodes_;            // every clock net, built once by the first release-safe group that needs it
    bool clockNodesBuilt_ = false;
    std::set<uint32_t> releaseSafeFlops_;              // registers of top-level-reset groups whose release is safe at the first edge (rdc_release.cpp)
    std::unique_ptr<ReleaseEvaluator> evaluator_;
    bool releaseSafeGroup(NodeId root, uint32_t dom, bool activeLow, const std::vector<uint32_t>& users);   // rdc_release.cpp
    std::set<uint32_t> fullSyncStages_;                // stages of recognised reset synchronisers (depth >= minStages_, matching receiver domain & edge)
    std::set<std::pair<NodeId, uint32_t>> rdc007Exempt_;   // (resetRoot, flopIdx) exempt from RDC-007 (shape-based reset sync, shared-reset data sync, constant-D capture into data sync)
    std::unordered_set<NodeId> blackBoxNodes_;
    std::unordered_set<NodeId> clockDomainRoots_;
    mutable std::vector<int32_t> dataSyncLastMemo_;
    AssertedUses assertedUses_;
    model::ContractBindingResult contracts_;           // the reset_contracts binding, computed once in ensure()
    mutable std::set<NodeId> traceTruncated_;          // assertion sources whose synchroniser hop chain outran the trace bound
    // Driver expression of a net (continuous assignment or declaration initialiser), indexed once from the model's pair facts.
    mutable std::map<const slang::ast::Symbol*, const slang::ast::Expression*> driverByNet_;
    // Part-select assignments (`assign r[1] = ...`): the bit range each drives. Unknown indices count as the whole net.
    struct PartDriver { uint64_t lo = 0, hi = 0; const slang::ast::Expression* rhs = nullptr; };
    mutable std::map<const slang::ast::Symbol*, std::vector<PartDriver>> partDrivers_;
    mutable bool driversIndexed_ = false;
    const slang::ast::Expression* driverExpression(const slang::ast::Symbol* net) const;   // rdc_structure.cpp
    // Whether an X or Z literal in the driver of `net` can reach bit `bit` (or any bit when `whole`): rdc_structure.cpp
    bool unknownLiteralAtBit(const slang::ast::Symbol* net, uint64_t bit, bool whole) const;
    ConeFinder* cones_ = nullptr;
    void structure(RuleContext& ctx);                  // rdc_structure.cpp
    void resetContracts(RuleContext& ctx);             // rdc_structure.cpp: CST-016 binding of the reset_contracts section
    NodeId assertionSource(NodeId root) const;         // follows reset synchronisers back to the asynchronous assertion source
    NodeId copyRootOf(NodeId n) const;                 // follows exact copies and bit inversions back to the net they copy (identity, not polarity)
    int32_t dataSyncLastStage(uint32_t flopIdx) const; // rdc_structure.cpp: last stage index of a recognised CDC data synchroniser, or -1
    bool isDataSyncFirstStage(uint32_t flopIdx) const { return dataSyncLastStage(flopIdx) >= 0; }
    bool inputHasFromException(NodeId inputNode, const Config& cfg) const;   // rdc_structure.cpp: covered by -from set_false_path / set_multicycle_path
    bool inputTimedToDomain(NodeId inputNode, uint32_t dom, const Config& cfg) const;   // rdc_structure.cpp: applied input delay on dom and no -from exception
    bool isRdc007Exempt(NodeId resetRoot, uint32_t flopIdx) const {
        return fullSyncStages_.count(flopIdx) > 0 || rdc007Exempt_.count({resetRoot, flopIdx}) > 0;
    }
    std::set<NodeId> asyncSources(const Flop& f) const;
    const Model* m_ = nullptr;
    std::unique_ptr<ClockRelations> rel_;

    void add(Diagnostic d) { findings[d.ruleId].push_back(std::move(d)); }

    const Flop* flopBehind(NodeId n, uint32_t* idx = nullptr) const { return m_->flopBehind(n, idx); }

    /// Walks a candidate reset synchroniser from its last stage back to the constant-D first stage (see syncChainOf).
    SyncInfo syncChain(uint32_t last) const { return syncChainOf(*m_, assertedUses_, last); }

    Diagnostic mk(const char* rule, Severity sev, std::string msg, const SourceLocation& loc, std::string hier, Confidence conf) {
        Diagnostic d;
        d.ruleId = rule;
        d.severity = sev;
        d.message = std::move(msg);
        d.location = loc;
        d.hierarchy = std::move(hier);
        d.confidence = conf;
        return d;
    }

    void analyse(RuleContext& ctx, int minStages) {
        // Registers that are themselves stages of a recognised reset synchroniser legitimately take the raw
        // asynchronous reset; they are not reported as "unsynchronised users".
        std::map<NodeId, std::vector<uint32_t>> rawUsersOf;
        for (uint32_t i = 0; i < m_->flops.size(); i++)
            for (auto& r : m_->flops[i].resets) rawUsersOf[r.root.node].push_back(i);
        std::set<uint32_t> syncStages;
        fullSyncStages_.clear();
        rdc007Exempt_.clear();
        for (const auto& [rootNode, users] : rawUsersOf) {
            if (m_->traceRoot(rootNode).kind != RootKind::Logic) continue;
            uint32_t ri = 0;
            const Flop* rf = flopBehind(rootNode, &ri);
            if (!rf || !rf->clockResolved) continue;
            SyncInfo si = syncChain(ri);
            if (!si.recognised) continue;
            for (uint32_t st : si.stages) syncStages.insert(st);
            if (si.depth >= minStages) {
                bool receiversMatch = !users.empty();
                for (uint32_t ui : users) {
                    const Flop& uf = m_->flops[ui];
                    if (!uf.clockResolved || rel_->relation(rf->domain, uf.domain) != Relation::Same || uf.edge != rf->edge) {
                        receiversMatch = false;
                        break;
                    }
                }
                if (receiversMatch)
                    for (uint32_t st : si.stages) fullSyncStages_.insert(st);
            }
        }
        auto aliasForward = [&](NodeId start, std::unordered_set<NodeId>& out) {
            std::vector<NodeId> work{start};
            out.insert(start);
            while (!work.empty()) {
                NodeId n = work.back();
                work.pop_back();
                if (n >= m_->out.size()) continue;
                for (auto ei : m_->out[n]) {
                    const Edge& e = m_->edges[ei];
                    if (e.type == EdgeType::Comb && e.exact && !e.ctrl && !m_->isDeadEdge(e) && !m_->isConstantProc(e.proc) &&
                        out.insert(e.to).second)
                        work.push_back(e.to);
                }
            }
        };
        auto cleanSyncFlop = [&](uint32_t fi) -> bool {
            const Flop& f = m_->flops[fi];
            return f.clockResolved && !f.blocking && !f.hasIff && f.edge != 3 && !m_->procs[f.proc].bodyTiming &&
                   f.otherDomains.empty() && !m_->multiSequential.count(f.q) && !cones_->hasUncertainCalls(fi);
        };
        auto feedsOnlyNextStage = [&](uint32_t fromIdx, uint32_t nextIdx) -> bool {
            std::unordered_set<NodeId> cl;
            aliasForward(m_->flops[fromIdx].q, cl);
            bool sawNext = false;
            const NodeId nextQ = m_->flops[nextIdx].q;
            for (NodeId n : cl) {
                if ((n < m_->topOutput.size() && m_->topOutput[n]) || blackBoxNodes_.count(n) || clockDomainRoots_.count(n))
                    return false;
                if (n >= m_->out.size()) continue;
                for (auto ei : m_->out[n]) {
                    const Edge& e = m_->edges[ei];
                    if (m_->isDeadEdge(e) || m_->isConstantProc(e.proc)) continue;
                    if (e.type == EdgeType::AsyncCtl) return false;
                    if (e.type == EdgeType::Comb) {
                        if (!(e.exact && !e.ctrl)) return false;
                    } else if (e.type == EdgeType::Seq) {
                        if (e.to != nextQ || !e.exact || e.ctrl) return false;
                        sawNext = true;
                    }
                }
            }
            return sawNext;
        };
        // (2b) Recognise reset-synchroniser chains by their own shape in the receiving domain regardless of consumer
        // (e.g. output feeding a scan mux or a reset-release valid flag): non-final stages must only feed the next stage.
        for (uint32_t ri = 0; ri < m_->flops.size(); ++ri) {
            if (!m_->flops[ri].clockResolved || m_->flops[ri].resets.empty()) continue;
            SyncInfo si = syncChain(ri);
            if (!si.recognised || si.depth < minStages || si.stages.empty()) continue;
            bool cleanNonFinal = true;
            for (size_t k = 1; k < si.stages.size(); ++k) {
                if (!feedsOnlyNextStage(si.stages[k], si.stages[k - 1])) {
                    cleanNonFinal = false;
                    break;
                }
            }
            if (!cleanNonFinal) continue;
            for (uint32_t st : si.stages) {
                syncStages.insert(st);
                for (const auto& r : m_->flops[st].resets) rdc007Exempt_.emplace(r.root.node, st);
            }
        }
        // (2c) Clean same-domain, same-edge nonblocking register chains of depth >= minStages where every stage is
        // asynchronously reset by R with the same polarity and binary reset value (even when stage 1's D is data).
        for (uint32_t firstIdx = 0; firstIdx < m_->flops.size(); ++firstIdx) {
            if (!cleanSyncFlop(firstIdx) || m_->flops[firstIdx].resets.empty()) continue;
            const Flop& firstF = m_->flops[firstIdx];
            // Stage 1 must not have direct self-feedback.
            std::unordered_set<NodeId> firstClosure;
            aliasForward(firstF.q, firstClosure);
            bool selfFeedback = false;
            for (auto ei : m_->in[firstF.q]) {
                const Edge& e = m_->edges[ei];
                if (e.type == EdgeType::Seq && !e.ctrl && !m_->isDeadEdge(e) && firstClosure.count(e.from)) {
                    selfFeedback = true;
                    break;
                }
            }
            if (selfFeedback) continue;
            std::vector<uint32_t> chain{firstIdx};
            std::set<uint32_t> visited{firstIdx};
            uint32_t cur = firstIdx;
            while (chain.size() < 16) {
                std::unordered_set<NodeId> cl;
                aliasForward(m_->flops[cur].q, cl);
                bool escape = false;
                std::map<NodeId, std::vector<const Edge*>> seqTargets;
                for (NodeId n : cl) {
                    if ((n < m_->topOutput.size() && m_->topOutput[n]) || blackBoxNodes_.count(n) || clockDomainRoots_.count(n)) {
                        escape = true;
                        break;
                    }
                    if (n >= m_->out.size()) continue;
                    for (auto ei : m_->out[n]) {
                        const Edge& e = m_->edges[ei];
                        if (m_->isDeadEdge(e) || m_->isConstantProc(e.proc)) continue;
                        if (e.type == EdgeType::AsyncCtl) { escape = true; break; }
                        if (e.type == EdgeType::Comb) {
                            if (!(e.exact && !e.ctrl)) { escape = true; break; }
                        } else if (e.type == EdgeType::Seq) {
                            seqTargets[e.to].push_back(&e);
                        }
                    }
                    if (escape) break;
                }
                if (escape || seqTargets.size() != 1) break;
                const auto& [tq, es] = *seqTargets.begin();
                auto fit = m_->flopOfQ.find(tq);
                if (fit == m_->flopOfQ.end() || visited.count(fit->second)) break;
                const uint32_t nextIdx = fit->second;
                if (!cleanSyncFlop(nextIdx)) break;
                const Flop& nextF = m_->flops[nextIdx];
                if (rel_->relation(nextF.domain, firstF.domain) != Relation::Same || nextF.edge != firstF.edge) break;
                bool cleanIn = true;
                for (const Edge* e : es) if (!e->exact || e->ctrl) { cleanIn = false; break; }
                for (auto ei : m_->in[tq]) {
                    const Edge& e = m_->edges[ei];
                    if (e.type != EdgeType::Seq || m_->isDeadEdge(e)) continue;
                    if (e.ctrl || !e.exact || !cl.count(e.from)) { cleanIn = false; break; }
                }
                if (!cleanIn) break;
                chain.push_back(nextIdx);
                visited.insert(nextIdx);
                cur = nextIdx;
            }
            if (chain.size() == 1 && firstF.extraStages + 1 < minStages) continue;
            for (size_t ri = 0; ri < firstF.resets.size(); ++ri) {
                const char rv = ri < firstF.resetValues.size() ? firstF.resetValues[ri] : '?';
                const bool cond = ri < firstF.resetConditional.size() && firstF.resetConditional[ri];
                if ((rv != '0' && rv != '1') || cond) continue;
                const NodeId rNode = firstF.resets[ri].root.node;
                const bool rLow = firstF.resets[ri].activeLow;
                int matchedDepth = 0;
                size_t matchedLen = 0;
                for (uint32_t st : chain) {
                    const Flop& sf = m_->flops[st];
                    bool hasMatch = false;
                    for (size_t k = 0; k < sf.resets.size(); ++k) {
                        const char srv = k < sf.resetValues.size() ? sf.resetValues[k] : '?';
                        const bool scond = k < sf.resetConditional.size() && sf.resetConditional[k];
                        if (sf.resets[k].root.node == rNode && sf.resets[k].activeLow == rLow && srv == rv && !scond) {
                            hasMatch = true;
                            break;
                        }
                    }
                    if (!hasMatch) break;
                    matchedDepth += 1 + sf.extraStages;
                    matchedLen++;
                }
                if (matchedDepth >= minStages) {
                    for (size_t idx = 0; idx < matchedLen; ++idx) rdc007Exempt_.emplace(rNode, chain[idx]);
                }
            }
        }
        // (2d) Receiving bit whose released D is a binary constant and whose every fan-out is the first stage of a
        // recognised data synchroniser in the same clock domain (e.g. reset-event capture flop feeding a 2-flop sync).
        for (uint32_t ui = 0; ui < m_->flops.size(); ++ui) {
            if (!cleanSyncFlop(ui) || m_->flops[ui].resets.empty()) continue;
            const Flop& f = m_->flops[ui];
            if (!f.resetTransitionKnown) continue;
            const char released = f.releasedSource == kNoNode ? f.releasedValue : m_->constantValue(f.releasedSource);
            if (released != '0' && released != '1') continue;
            bool constOnlyD = true;
            for (auto ei : m_->in[f.q]) {
                const Edge& e = m_->edges[ei];
                if (e.type != EdgeType::Seq || m_->isDeadEdge(e)) continue;
                if (e.ctrl || !e.exact || m_->constantValue(e.from) != released) { constOnlyD = false; break; }
            }
            if (!constOnlyD) continue;
            std::unordered_set<NodeId> cl;
            aliasForward(f.q, cl);
            bool ok = true, sawSyncTarget = false;
            for (NodeId n : cl) {
                if ((n < m_->topOutput.size() && m_->topOutput[n]) || blackBoxNodes_.count(n) || clockDomainRoots_.count(n)) {
                    ok = false;
                    break;
                }
                if (n >= m_->out.size()) continue;
                for (auto ei : m_->out[n]) {
                    const Edge& e = m_->edges[ei];
                    if (m_->isDeadEdge(e) || m_->isConstantProc(e.proc)) continue;
                    if (e.type == EdgeType::AsyncCtl) { ok = false; break; }
                    if (e.type == EdgeType::Comb) {
                        if (!(e.exact && !e.ctrl)) { ok = false; break; }
                    } else if (e.type == EdgeType::Seq) {
                        if (e.ctrl || !(e.exact || e.flip) || e.approximate) { ok = false; break; }
                        auto fit = m_->flopOfQ.find(e.to);
                        if (fit == m_->flopOfQ.end()) { ok = false; break; }
                        const uint32_t targetIdx = fit->second;
                        const Flop& tf = m_->flops[targetIdx];
                        if (!tf.clockResolved || rel_->relation(tf.domain, f.domain) != Relation::Same || !isDataSyncFirstStage(targetIdx)) {
                            ok = false;
                            break;
                        }
                        bool soleData = true;
                        for (auto inEi : m_->in[e.to]) {
                            const Edge& inE = m_->edges[inEi];
                            if (inE.type != EdgeType::Seq || inE.ctrl || m_->isDeadEdge(inE)) continue;
                            if (!cl.count(inE.from)) { soleData = false; break; }
                        }
                        if (!soleData) { ok = false; break; }
                        sawSyncTarget = true;
                    }
                }
                if (!ok) break;
            }
            if (ok && sawSyncTarget) {
                for (const auto& r : f.resets) rdc007Exempt_.emplace(r.root.node, ui);
            }
        }
        shapeSyncStages_.clear();
        for (uint32_t st : syncStages)
            if (isShapeSyncStage(*m_, assertedUses_, minStages, st)) shapeSyncStages_.insert(st);
        releaseSafeFlops_.clear();
        // reset root -> clock domain -> users
        struct Use { std::vector<uint32_t> flops; bool activeLow = false; };
        std::map<NodeId, std::map<uint32_t, Use>> byRoot;
        std::map<NodeId, Root> rootInfo;
        auto isCopyOfTopPort = [&](NodeId n) {
            const NodeId cr = copyRootOf(n);
            return cr < m_->topInput.size() && m_->topInput[cr];
        };
        for (uint32_t i = 0; i < m_->flops.size(); i++) {
            const Flop& f = m_->flops[i];
            if (!f.clockResolved) continue;
            for (auto& r : f.resets) {
                if ((r.root.kind == RootKind::TopPort || isCopyOfTopPort(r.root.node)) && syncStages.count(i)) continue;   // the synchroniser itself
                const char tied = tiedResetValue(*m_, r.root.node);
                if (tied == '0' || tied == '1') continue;   // constant-tied reset: handled in RDC-001, never releases asynchronously
                auto& u = byRoot[r.root.node][f.domain];
                u.flops.push_back(i);
                u.activeLow = r.activeLow;
                rootInfo[r.root.node] = r.root;
            }
        }
        evaluated["RDC-003"] = evaluated["RDC-006"] = evaluated["RDC-007"] = byRoot.size();
        for (auto& [node, doms] : byRoot) {
            const Root& root = rootInfo[node];
            const std::string bn = rel_->rtlBitName(node);
            std::string rname = bn.empty() ? m_->nameOf(node) : bn;
            for (auto& [dom, use] : doms) {
                const Flop& f0 = m_->flops[use.flops[0]];
                const ProcInfo& p0 = m_->procs[f0.proc];
                std::string dname = m_->domains[dom].name;
                auto base = [&](const char* rule, Severity sev, std::string msg, Confidence conf) {
                    Diagnostic d = mk(rule, sev, std::move(msg), p0.loc, p0.instancePath, conf);
                    d.signals = {rname, dname};
                    d.identityKey = rname + ":" + dname + ":" + std::to_string(f0.edge) + ":" + rel_->clockName(dom);
                    d.evidence.push_back({"reset_source", rname + " (" + (use.activeLow ? "active-low" : "active-high") + " by edge polarity)", {}});
                    d.evidence.push_back({"reset_users", std::to_string(use.flops.size()) + " register bit(s) in clock domain '" + dname + "', e.g. " + m_->nameOf(f0.q), p0.loc});
                    return d;
                };
                std::string who = "asynchronous reset '" + rname + "' of " + std::to_string(use.flops.size()) +
                                  " register bit(s) in clock domain '" + dname + "'";
                if (root.kind == RootKind::TopPort) {
                    if (inputTimedToDomain(root.node, dom, ctx.config)) {
                        Diagnostic d = base("RDC-003", Severity::Info, who + " comes from a primary input with release timed by the declared input delay on clock '" +
                                            rel_->clockName(dom) + "'", Confidence::Informational);
                        d.evidence.push_back({"trust_model", "release timed by the declared input delay; recovery/removal timing, minimum pulse width and external synchronisation are not analysed (RDC-013)", {}});
                        d.proofState = ProofState::NotAttempted;
                        add(std::move(d));
                        continue;
                    }
                    if (releaseSafeGroup(root.node, dom, use.activeLow, use.flops)) {
                        Diagnostic d = base("RDC-003", Severity::Info, who + " comes from a primary input whose release is safe at the first clock edge: every register is a reset-synchroniser stage, "
                                            "holds its reset value, or has its change masked", Confidence::Informational);
                        d.evidence.push_back({"trust_model", "the release is ambiguous at no more than one edge of the domain clock; assumes the recovery/removal window is shorter than a clock period and that a controlling constant at a gate masks a metastable input (RDC-013)", {}});
                        d.proofState = ProofState::NotAttempted;
                        add(std::move(d));
                        continue;
                    }
                    Diagnostic d = base("RDC-003", Severity::Warning, who + " comes straight from a primary input: its release is not synchronised inside the design", Confidence::Unresolved);
                    d.nextStep = "Add a reset synchroniser per clock domain, or document that the input is already synchronised (no reset constraints are interpreted yet).";
                    add(std::move(d));
                    ctx.report.markIncomplete("reset-extraction", "reset '" + rname + "' release synchronisation is unresolved");
                    continue;
                }
                if (root.kind == RootKind::Logic) {
                    uint32_t ri = 0;
                    const Flop* rf = flopBehind(root.node, &ri);
                    if (!rf) {
                        Diagnostic d = base("RDC-003", Severity::Warning, who + " is generated by combinational logic (not a register): glitches and release timing are unresolved", Confidence::Unresolved);
                        d.nextStep = "Register the reset in each destination clock domain through a reset synchroniser.";
                        add(std::move(d));
                        ctx.report.markIncomplete("reset-extraction", "reset '" + rname + "' is generated by logic");
                        continue;
                    }
                    if (!rf->clockResolved) {
                        add(base("RDC-003", Severity::Warning, who + " is generated by a register with an unidentified clock", Confidence::Unresolved));
                        ctx.report.markIncomplete("reset-extraction", "reset generator clock of '" + rname + "' unidentified");
                        continue;
                    }
                    Relation rel = rel_->relation(rf->domain, dom);
                    if (rel != Relation::Same) {
                        // Receiving register bits that are stages of a recognised reset synchroniser in the receiving
                        // domain are exempt: handing a reset from domain S into domain D's reset synchroniser is standard.
                        std::vector<uint32_t> nonSyncFlops;
                        for (uint32_t ui : use.flops) if (!isRdc007Exempt(node, ui)) nonSyncFlops.push_back(ui);
                        if (nonSyncFlops.empty()) continue;
                        const Flop& nf0 = m_->flops[nonSyncFlops[0]];
                        const ProcInfo& np0 = m_->procs[nf0.proc];
                        const std::string nwho = "asynchronous reset '" + rname + "' of " + std::to_string(nonSyncFlops.size()) +
                                                 " register bit(s) in clock domain '" + dname + "'";
                        Severity sev = rel == Relation::Async ? Severity::Error : rel == Relation::Exclusive ? Severity::Info : Severity::Warning;
                        Diagnostic d = mk("RDC-007", sev, nwho + " is generated by a register in clock domain '" + m_->domains[rf->domain].name +
                                          "' [clock relationship " + relName(rel) + "]: its release is asynchronous to the receiving clock",
                                          np0.loc, np0.instancePath,
                                          rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
                        d.signals = {rname, dname};
                        d.identityKey = rname + ":" + dname + ":" + std::to_string(nf0.edge) + ":" + rel_->clockName(dom);
                        d.evidence.push_back({"reset_source", rname + " (" + (use.activeLow ? "active-low" : "active-high") + " by edge polarity)", {}});
                        d.evidence.push_back({"reset_users", std::to_string(nonSyncFlops.size()) + " register bit(s) in clock domain '" + dname + "', e.g. " + m_->nameOf(nf0.q), np0.loc});
                        d.evidence.push_back({"domain_provenance", "reset generator register " + m_->nameOf(root.node) + " in domain " + m_->domains[rf->domain].name, m_->procs[rf->proc].loc});
                        {   // the generator's output also steers ordinary logic (a data signal used as an asynchronous control)
                            bool functional = false;
                            std::vector<NodeId> cl{m_->aliasRoot(root.node)};
                            std::unordered_set<NodeId> seenN{cl[0]};
                            for (size_t ci = 0; ci < cl.size() && !functional; ci++)
                                for (auto ei : m_->out[cl[ci]]) {
                                    const Edge& e = m_->edges[ei];
                                    if (e.type == EdgeType::AsyncCtl) continue;
                                    if (e.type == EdgeType::Comb && e.exact) { if (seenN.insert(e.to).second) cl.push_back(e.to); continue; }
                                    functional = true;
                                    break;
                                }
                            if (functional) d.evidence.push_back({"also_functional_data", "the generator register's output also feeds functional logic: a data signal used as an asynchronous control", {}});
                        }
                        d.nextStep = "Pass the reset through a synchroniser clocked by the receiving domain.";
                        add(std::move(d));
                        if (rel == Relation::Unknown) ctx.report.markIncomplete("clock-relationships", "reset '" + rname + "' crosses unrelated clocks");
                        continue;
                    }
                    SyncInfo si = syncChain(ri);
                    bool receiverEdgeMismatch = false;
                    for (auto ui : use.flops) {
                        if (m_->flops[ui].edge != rf->edge) { receiverEdgeMismatch = true; break; }
                    }
                    if (si.recognised && si.depth >= minStages) {
                        if (receiverEdgeMismatch) {
                            Diagnostic d = base("RDC-003", Severity::Warning, who + " is released through a reset synchroniser clocked on the opposite edge of domain '" + dname +
                                                "': half-cycle reset release timing is unresolved", Confidence::Unresolved);
                            d.nextStep = "Clock the reset synchroniser on the same clock edge as the receiving registers, or verify half-cycle recovery/removal timing externally.";
                            add(std::move(d));
                            ctx.report.markIncomplete("reset-extraction", "reset '" + rname + "' synchroniser edge differs from receiving register edge");
                        } else {
                            Diagnostic d = base("RDC-003", Severity::Info, who + " is released through a recognised " + std::to_string(si.depth) +
                                                "-stage reset synchroniser", Confidence::Informational);
                            d.evidence.push_back({"trust_model", "structural recognition: asynchronously asserted, synchronously released through a chain of "
                                                  "async-reset registers with a constant first-stage D in the receiving clock domain; recovery/removal "
                                                  "timing, reset pulse width and fan-out are not analysed (RDC-013)", {}});
                            d.proofState = ProofState::NotAttempted;
                            add(std::move(d));
                        }
                    } else if (si.recognised) {
                        Diagnostic d = base("RDC-006", Severity::Warning, who + " is released through a reset synchroniser of only " + std::to_string(si.depth) +
                                            " stage(s) (policy requires " + std::to_string(minStages) + ")", Confidence::Established);
                        d.nextStep = "Add stages or lower RDC-006.min_stages if the flops justify it.";
                        add(std::move(d));
                        if (receiverEdgeMismatch) {
                            Diagnostic e = base("RDC-003", Severity::Warning, who + " is released through a reset synchroniser clocked on the opposite edge of domain '" + dname +
                                                "': half-cycle reset release timing is unresolved", Confidence::Unresolved);
                            e.nextStep = "Clock the reset synchroniser on the same clock edge as the receiving registers, or verify half-cycle recovery/removal timing externally.";
                            add(std::move(e));
                            ctx.report.markIncomplete("reset-extraction", "reset '" + rname + "' synchroniser edge differs from receiving register edge");
                        }
                    } else {
                        Diagnostic d = base("RDC-003", Severity::Warning, who + " is generated by a register in the same clock domain, but the structure is not a recognised reset synchroniser (" + si.why + ")",
                                            Confidence::Unresolved);
                        d.nextStep = "Use a chain of async-reset registers with a constant first-stage D, or review the reset release manually.";
                        add(std::move(d));
                        if (si.logic) {
                            // Only flag inter-stage logic when the generator flop's D cone actually includes a same-domain register stage.
                            bool hasPrevSameDomainFlop = false;
                            std::unordered_set<NodeId> seenD;
                            std::vector<NodeId> workD;
                            for (auto ei : m_->in[rf->q]) {
                                const Edge& e = m_->edges[ei];
                                if (e.type == EdgeType::Seq && !e.ctrl) workD.push_back(e.from);
                            }
                            while (!workD.empty() && !hasPrevSameDomainFlop) {
                                NodeId cur = workD.back(); workD.pop_back();
                                if (!seenD.insert(cur).second) continue;
                                if (const auto* pf = m_->flopAt(cur)) {
                                    if (pf->clockResolved && pf->domain == rf->domain && pf->q != rf->q)
                                        hasPrevSameDomainFlop = true;
                                    continue;
                                }
                                for (auto ei : m_->in[cur]) {
                                    const Edge& e = m_->edges[ei];
                                    if (e.type == EdgeType::Comb) workD.push_back(e.from);
                                }
                            }
                            if (hasPrevSameDomainFlop)
                                add(base("RDC-006", Severity::Warning, who + " is generated by a register chain with logic or several sources feeding a stage: logic on a reset synchroniser "
                                         "defeats its purpose", Confidence::Heuristic));
                        }
                        ctx.report.markIncomplete("reset-extraction", "reset '" + rname + "' release structure not recognised");
                    }
                    continue;
                }
                const char* kind = root.kind == RootKind::BlackBox ? "an unmodeled black box" : root.kind == RootKind::Cycle ? "a combinational cycle" : "undriven";
                add(base("RDC-003", Severity::Warning, who + " originates from " + kind + ": its release behaviour is unresolved", Confidence::Unresolved));
                ctx.report.markIncomplete("reset-extraction", "reset '" + rname + "' originates from " + kind);
            }
        }
    }
};
} // namespace rdc
} // namespace sverity
