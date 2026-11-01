// Reset-domain analysis on the shared model: where each asynchronous reset comes from, whether its release is
// synchronised to the clock of the registers it resets, and whether it is generated in another clock domain.
//
// Policy (same as CDC): a reset whose release cannot be shown to be synchronised is a WARNING (unresolved), a reset
// generated in a clock domain declared asynchronous to its users is an ERROR, a recognised reset synchroniser is INFO
// with its trust model. Nothing is reported as safe.
#include <algorithm>
#include <map>
#include <set>

#include "domains/model.h"
#include "domains/relations.h"
#include "rules/rules.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"

namespace sverity {
namespace {

using namespace model;

struct SyncInfo {
    int depth = 0;
    bool recognised = false;   // every stage async-reset, chain in one domain, first stage D constant
    std::string why;           // reason when not recognised
};

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
        try { minStages = std::max(2, std::stoi(ctx.config.option("RDC-006.min_stages", "2"))); } catch (...) {}
        analyse(ctx, minStages);
    }
    void flush(const std::string& rule, RuleContext& ctx) {
        for (auto& d : findings[rule]) ctx.report.add(d);
        ctx.objectsEvaluated = evaluated[rule];
    }

private:
    const Model* m_ = nullptr;
    std::unique_ptr<ClockRelations> rel_;

    void add(Diagnostic d) { findings[d.ruleId].push_back(std::move(d)); }

    /// Follows exact aliases backwards to the register output that drives `n`, if any.
    const Flop* flopBehind(NodeId n, uint32_t* idx = nullptr) const {
        for (int guard = 0; guard < 64; guard++) {
            auto it = m_->flopOfQ.find(n);
            if (it != m_->flopOfQ.end()) { if (idx) *idx = it->second; return &m_->flops[it->second]; }
            if (m_->in[n].size() != 1) return nullptr;
            const Edge& e = m_->edges[m_->in[n][0]];
            if (!(e.type == EdgeType::Comb && e.exact)) return nullptr;
            n = e.from;
        }
        return nullptr;
    }

    /// Walks a candidate reset synchroniser from its last stage back to the constant-D first stage.
    SyncInfo syncChain(uint32_t last) const {
        SyncInfo si;
        std::set<uint32_t> seen;
        uint32_t cur = last;
        for (int guard = 0; guard < 16; guard++) {
            const Flop& f = m_->flops[cur];
            if (!seen.insert(cur).second) { si.why = "cyclic register chain"; return si; }
            if (!f.clockResolved) { si.why = "a stage has no identified clock"; return si; }
            if (f.resets.empty()) { si.why = "a stage has no asynchronous reset/set (assertion would not be asynchronous)"; return si; }
            si.depth++;
            std::vector<const Edge*> data;
            for (auto ei : m_->in[f.q]) {
                const Edge& e = m_->edges[ei];
                if (e.type == EdgeType::Seq) data.push_back(&e);
            }
            if (data.empty()) { si.recognised = true; return si; }      // D is constant: reached the first stage
            if (data.size() != 1 || !data[0]->exact) { si.why = "a stage's data input is not a plain previous stage or a constant"; return si; }
            uint32_t prev = 0;
            const Flop* pf = flopBehind(data[0]->from, &prev);
            if (!pf) { si.why = "the chain is fed by a non-register source (data input is not constant)"; return si; }
            if (!pf->clockResolved || pf->domain != f.domain) { si.why = "stages are clocked by different clocks"; return si; }
            cur = prev;
        }
        si.why = "chain longer than the analysis limit";
        return si;
    }

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
        std::set<uint32_t> syncStages;
        for (uint32_t i = 0; i < m_->flops.size(); i++) {
            const Flop& f = m_->flops[i];
            for (auto& r : f.resets) {
                if (r.root.kind != RootKind::Logic) continue;
                uint32_t ri = 0;
                const Flop* rf = flopBehind(r.root.node, &ri);
                if (!rf || !rf->clockResolved) continue;
                SyncInfo si = syncChain(ri);
                if (!si.recognised) continue;
                uint32_t cur = ri;
                for (int g = 0; g < 16; g++) {
                    syncStages.insert(cur);
                    std::vector<const Edge*> data;
                    for (auto ei : m_->in[m_->flops[cur].q]) if (m_->edges[ei].type == EdgeType::Seq) data.push_back(&m_->edges[ei]);
                    if (data.size() != 1) break;
                    uint32_t prev = 0;
                    if (!flopBehind(data[0]->from, &prev)) break;
                    cur = prev;
                }
            }
        }
        // reset root -> clock domain -> users
        struct Use { std::vector<uint32_t> flops; bool activeLow = false; };
        std::map<NodeId, std::map<uint32_t, Use>> byRoot;
        std::map<NodeId, Root> rootInfo;
        for (uint32_t i = 0; i < m_->flops.size(); i++) {
            const Flop& f = m_->flops[i];
            if (!f.clockResolved) continue;
            for (auto& r : f.resets) {
                if (r.root.kind == RootKind::TopPort && syncStages.count(i)) continue;   // the synchroniser itself
                auto& u = byRoot[r.root.node][f.domain];
                u.flops.push_back(i);
                u.activeLow = r.activeLow;
                rootInfo[r.root.node] = r.root;
            }
        }
        evaluated["RDC-003"] = evaluated["RDC-006"] = evaluated["RDC-007"] = byRoot.size();
        for (auto& [node, doms] : byRoot) {
            const Root& root = rootInfo[node];
            std::string rname = stripBit(m_->nameOf(node));
            for (auto& [dom, use] : doms) {
                const Flop& f0 = m_->flops[use.flops[0]];
                const ProcInfo& p0 = m_->procs[f0.proc];
                std::string dname = m_->domains[dom].name;
                auto base = [&](const char* rule, Severity sev, std::string msg, Confidence conf) {
                    Diagnostic d = mk(rule, sev, std::move(msg), p0.loc, p0.instancePath, conf);
                    d.signals = {rname, dname};
                    d.evidence.push_back({"reset_source", rname + " (" + (use.activeLow ? "active-low" : "active-high") + " by edge polarity)", {}});
                    d.evidence.push_back({"reset_users", std::to_string(use.flops.size()) + " register bit(s) in clock domain '" + dname + "', e.g. " + m_->nameOf(f0.q), p0.loc});
                    return d;
                };
                std::string who = "asynchronous reset '" + rname + "' of " + std::to_string(use.flops.size()) +
                                  " register bit(s) in clock domain '" + dname + "'";
                if (root.kind == RootKind::TopPort) {
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
                        Severity sev = rel == Relation::Async ? Severity::Error : rel == Relation::Exclusive ? Severity::Info : Severity::Warning;
                        Diagnostic d = base("RDC-007", sev, who + " is generated by a register in clock domain '" + m_->domains[rf->domain].name +
                                            "' [clock relationship " + relName(rel) + "]: its release is asynchronous to the receiving clock",
                                            rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
                        d.evidence.push_back({"domain_provenance", "reset generator register " + m_->nameOf(root.node) + " in domain " + m_->domains[rf->domain].name, m_->procs[rf->proc].loc});
                        d.nextStep = "Pass the reset through a synchroniser clocked by the receiving domain.";
                        add(std::move(d));
                        if (rel == Relation::Unknown) ctx.report.markIncomplete("clock-relationships", "reset '" + rname + "' crosses unrelated clocks");
                        continue;
                    }
                    SyncInfo si = syncChain(ri);
                    if (si.recognised && si.depth >= minStages) {
                        Diagnostic d = base("RDC-003", Severity::Info, who + " is released through a recognised " + std::to_string(si.depth) +
                                            "-stage reset synchroniser", Confidence::Informational);
                        d.evidence.push_back({"trust_model", "structural recognition: asynchronously asserted, synchronously released through a chain of "
                                              "async-reset registers with a constant first-stage D in the receiving clock domain; recovery/removal "
                                              "timing, reset pulse width and fan-out are not analysed (RDC-013)", {}});
                        d.proofState = ProofState::NotAttempted;
                        add(std::move(d));
                    } else if (si.recognised) {
                        Diagnostic d = base("RDC-006", Severity::Warning, who + " is released through a reset synchroniser of only " + std::to_string(si.depth) +
                                            " stage(s) (policy requires " + std::to_string(minStages) + ")", Confidence::Established);
                        d.nextStep = "Add stages or lower RDC-006.min_stages if the flops justify it.";
                        add(std::move(d));
                    } else {
                        Diagnostic d = base("RDC-003", Severity::Warning, who + " is generated by a register in the same clock domain, but the structure is not a recognised reset synchroniser (" + si.why + ")",
                                            Confidence::Unresolved);
                        d.nextStep = "Use a chain of async-reset registers with a constant first-stage D, or review the reset release manually.";
                        add(std::move(d));
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

class RdcRule final : public Rule {
public:
    RdcRule(std::string id, std::shared_ptr<RdcShared> sh, std::string gap) : id_(std::move(id)), sh_(std::move(sh)), gap_(std::move(gap)) {}
    std::string_view id() const override { return id_; }
    RuleState completeness() const override { return RuleState::Partial; }
    std::string partialReason() const override { return gap_; }
    void run(RuleContext& ctx) override { sh_->ensure(ctx); sh_->flush(id_, ctx); }

private:
    std::string id_;
    std::shared_ptr<RdcShared> sh_;
    std::string gap_;
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeRdcRules() {
    auto sh = std::make_shared<RdcShared>();
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<RdcRule>("RDC-003", sh,
        "reset release is classified per (reset root, clock domain) from event-list resets only; synchronous resets, reset-gating logic and reset constraints are not considered"));
    v.push_back(std::make_unique<RdcRule>("RDC-006", sh,
        "stage count of a recognised chain only; logic and fan-out on the synchroniser stages are not checked"));
    v.push_back(std::make_unique<RdcRule>("RDC-007", sh,
        "resets generated by a register in another clock domain; resets generated by logic fed from several domains are only reported as RDC-003 warnings"));
    return v;
}

} // namespace sverity
