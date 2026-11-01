// Shared CDC / reset / constraint analysis over the design model (RFC section 2: "shared analysis pass").
// One lazily-built analysis feeds all rule objects of this family; no rule re-parses RTL.
//
// Honesty policy implemented here:
//  * unknown clock relationship          -> WARNING, confidence "unresolved" (never "safe")
//  * recognised 2-flop synchroniser      -> INFO with the trust-model assumptions spelled out
//  * heuristic classifications           -> confidence "heuristic"
//  * every graph-precision fallback      -> CST-010 WARNING + scope INCOMPLETE
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

#include "domains/model.h"
#include "domains/relations.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "rules/rules.h"
#include "sverity/waiver.h"   // globMatch

namespace sverity {
namespace {

using namespace model;

struct Chain {
    int depth = 1;
    bool linkLogic = false;      // logic between stage 1 and stage 2
    bool extraFanout = false;    // stage-1 output also feeds something other than stage 2
    std::vector<uint32_t> stages;   // flop indices, stage 1 first
};

struct Finding {
    Diagnostic d;
};

class Shared {
public:
    Model* m = nullptr;     // owned by the Design (shared with other rule families)
    bool built = false;
    std::map<std::string, std::vector<Diagnostic>> findings;
    std::map<std::string, uint64_t> evaluated;
    std::unique_ptr<ClockRelations> rel_;
    Config const* cfg = nullptr;
    Report* report = nullptr;

    void ensure(RuleContext& ctx) {
        if (built) return;
        built = true;
        cfg = &ctx.config;
        report = &ctx.report;
        for (const char* sc : {"clock-extraction", "reset-extraction", "clock-relationships", "constraints",
                               "graph-precision", "blackboxes"})
            report->declareScope(sc);
        m = &getModel(ctx.design);
        report->setObjectCount("instances", m->numInstances);
        report->setObjectCount("procedures_analysed", m->numProcedures);
        report->setObjectCount("sequential_procedures", m->numSeqProcedures);
        report->setObjectCount("register_bits", m->flops.size());
        report->setObjectCount("clock_domains", m->domains.size());
        report->setObjectCount("graph_nodes", m->nodes.size());
        report->setObjectCount("graph_edges", m->edges.size());
        report->setObjectCount("blackbox_instances", m->blackBoxes.size());
        report->setObjectCount("precision_fallbacks", m->fallbacks.size());
        mapConstraints();
        analyseFallbacks();
        analyseBlackBoxes();
        analyseConstraints();
        analyseClocks();
        analyseResets();
        analyseCrossings();
    }

    void flush(const std::string& rule, RuleContext& ctx) {
        for (auto& d : findings[rule]) ctx.report.add(d);
        ctx.objectsEvaluated = evaluated[rule];
    }

private:
    // ------------------------------------------------------------ helpers
    Diagnostic mk(const char* rule, Severity sev, std::string msg, SourceLocation loc = {}, std::string hier = {},
                  Confidence conf = Confidence::Established) {
        Diagnostic d;
        d.ruleId = rule;
        d.severity = sev;
        d.message = std::move(msg);
        d.location = std::move(loc);
        d.hierarchy = std::move(hier);
        d.confidence = conf;
        return d;
    }
    void add(Diagnostic d) { findings[d.ruleId].push_back(std::move(d)); }

    const ProcInfo& procOf(const Flop& f) const { return m->procs[f.proc]; }
    std::string flopName(const Flop& f) const { return m->nameOf(f.q); }

    // ------------------------------------------------------------ constraints <-> domains
    void mapConstraints() { rel_ = std::make_unique<ClockRelations>(*m, *cfg); }
    Relation relation(uint32_t a, uint32_t b) const { return rel_->relation(a, b); }
    const std::string& domainClockName(uint32_t d) const { return rel_->clockName(d); }
    bool nameMatches(const std::string& kind, const std::string& pattern, NodeId n, const std::string& full) const {
        return rel_->nameMatches(kind, pattern, n, full);
    }

    // ------------------------------------------------------------ CST-010 / CST-005
    void analyseFallbacks() {
        std::map<std::string, std::vector<const Fallback*>> byCat;
        for (auto& f : m->fallbacks) byCat[f.category].push_back(&f);
        evaluated["CST-010"] = m->fallbacks.size();
        for (auto& [cat, v] : byCat) {
            Diagnostic d = mk("CST-010", Severity::Warning,
                              std::to_string(v.size()) + " precision fallback(s) of kind '" + cat +
                              "' in the connectivity/clock/reset model; results involving them are not conclusive",
                              v[0]->loc, {}, Confidence::Unresolved);
            for (size_t i = 0; i < v.size() && i < 8; i++) d.evidence.push_back({"fallback", v[i]->detail, v[i]->loc});
            if (v.size() > 8) d.evidence.push_back({"fallback", "... and " + std::to_string(v.size() - 8) + " more", {}});
            d.nextStep = "Restructure the construct or review it manually; the fallback detail above names what is not modelled.";
            add(std::move(d));
            report->markIncomplete("graph-precision", cat + ": " + std::to_string(v.size()) + " occurrence(s)");
        }
    }
    void analyseBlackBoxes() {
        evaluated["CST-005"] = m->blackBoxes.size();
        std::map<std::string, std::vector<const BlackBoxInst*>> byMod;
        for (auto& b : m->blackBoxes) byMod[b.module].push_back(&b);
        for (auto& [mod, v] : byMod) {
            Diagnostic d = mk("CST-005", Severity::Warning,
                              std::to_string(v.size()) + " instance(s) of unknown module '" + mod +
                              "' (black box, UNMODELED): its clock/reset/CDC behaviour is not analysed",
                              v[0]->loc, v[0]->hierarchy, Confidence::Unresolved);
            for (size_t i = 0; i < v.size() && i < 8; i++)
                d.evidence.push_back({"blackbox_instance", v[i]->hierarchy, v[i]->loc});
            d.nextStep = "Provide the module source or a model with clock/reset/CDC metadata.";
            add(std::move(d));
            report->markIncomplete("blackboxes", "unmodeled black box '" + mod + "'");
        }
    }

    // ------------------------------------------------------------ CST-009 / CST-018 / CST-011 / CST-002
    void analyseConstraints() {
        auto& c = cfg->constraints;
        evaluated["CST-009"] = c.unsupported.size();
        evaluated["CST-018"] = c.problems.size();
        std::map<std::string, std::vector<const sdc::Unsupported*>> byCmd;
        for (auto& u : c.unsupported) byCmd[u.command].push_back(&u);
        for (auto& [cmd, v] : byCmd) {
            Diagnostic d = mk("CST-009", Severity::Warning,
                              "SDC command '" + cmd + "' is not interpreted by SVerity (" + std::to_string(v.size()) +
                              " occurrence(s)); the constraint it expresses is NOT applied",
                              v[0]->loc, {}, Confidence::Unresolved);
            d.evidence.push_back({"sdc_command", v[0]->text, v[0]->loc});
            d.nextStep = "Check whether the missing constraint changes any CDC conclusion; supported commands are listed in docs.";
            add(std::move(d));
            report->markIncomplete("constraints", "unsupported SDC command '" + cmd + "'");
        }
        for (auto& p : c.problems) {
            add(mk("CST-018", Severity::Warning, "constraint parse problem: " + p.message, p.loc, {}, Confidence::Unresolved));
            report->markIncomplete("constraints", "constraint parse problem: " + p.message);
        }
        if (!cfg->constraintsProvided) {
            report->markIncomplete("constraints", "no constraint file provided: clock relationships are unknown");
        }
        // CST-002: clock source patterns that match no design object
        evaluated["CST-002"] = c.clocks.size();
        if (!c.clocks.empty()) {
            std::vector<std::string> names;
            for (NodeId i = 0; i < m->nodes.size(); i++) names.push_back(stripBit(m->nameOf(i)));
            for (auto& ck : c.clocks)
                for (auto& s : ck.sources) {
                    bool hit = false;
                    for (NodeId i = 0; i < names.size() && !hit; i++) hit = nameMatches(s.kind, s.pattern, i, names[i]);
                    if (!hit) {
                        add(mk("CST-002", Severity::Warning,
                               "clock constraint '" + ck.name + "' names '" + s.pattern + "' which resolves to no design object",
                               ck.loc, {}, Confidence::Unresolved));
                        report->markIncomplete("constraints", "constraint object '" + s.pattern + "' does not resolve");
                    }
                }
        }
    }

    // ------------------------------------------------------------ CDC-001 / CDC-002 / CST-011
    void analyseClocks() {
        evaluated["CDC-001"] = m->flops.size();
        evaluated["CDC-002"] = m->domains.size();
        evaluated["CST-011"] = m->domains.size();
        std::map<uint32_t, uint32_t> unresolvedPerProc;
        for (auto& f : m->flops)
            if (!f.clockResolved) unresolvedPerProc[f.proc]++;
        for (auto& [proc, n] : unresolvedPerProc) {
            Diagnostic d = mk("CDC-001", Severity::Warning,
                              "sequential block has " + std::to_string(n) + " register bit(s) whose clock could not be identified",
                              m->procs[proc].loc, m->procs[proc].instancePath, Confidence::Unresolved);
            d.nextStep = "Use a single clock edge event; reset events must be tested in the first if/else of the block.";
            add(std::move(d));
            report->markIncomplete("clock-extraction", "unidentified clock in sequential block at " +
                                   m->procs[proc].loc.file + ":" + std::to_string(m->procs[proc].loc.line));
        }
        std::map<uint32_t, uint32_t> perDomain;
        for (auto& f : m->flops) if (f.clockResolved) perDomain[f.domain]++;
        for (size_t i = 0; i < m->domains.size(); i++) {
            auto& dom = m->domains[i];
            const char* kind = dom.kind == RootKind::TopPort ? "top-level port" : dom.kind == RootKind::Logic ? "logic-generated"
                             : dom.kind == RootKind::BlackBox ? "black-box output" : dom.kind == RootKind::Cycle ? "cyclic" : "undriven";
            Diagnostic d = mk("CDC-001", Severity::Info,
                              "clock domain '" + dom.name + "' (" + kind + "): " + std::to_string(perDomain[i]) + " register bit(s)",
                              {}, {}, Confidence::Informational);
            d.proofState = ProofState::NotApplicable;
            if (!domainClockName(i).empty()) d.evidence.push_back({"domain_provenance", "constrained as SDC clock '" + domainClockName(i) + "'", {}});
            add(std::move(d));
            if (dom.kind != RootKind::TopPort) {
                Diagnostic g = mk(dom.kind == RootKind::Logic ? "CDC-002" : "CDC-001", Severity::Warning,
                                  std::string("clock '") + dom.name + "' is not a primary input: it is " + kind +
                                  "; its relationship to other clocks is unresolved",
                                  {}, {}, Confidence::Unresolved);
                g.nextStep = "Declare it with create_generated_clock / clock groups, or model the clock-generation cell.";
                add(std::move(g));
                report->markIncomplete("clock-extraction", std::string("clock '") + dom.name + "' is " + kind);
            }
            if (domainClockName(i).empty()) {
                Diagnostic g = mk("CST-011", Severity::Warning,
                                  "clock '" + dom.name + "' (" + std::to_string(perDomain[i]) + " register bit(s)) has no clock constraint",
                                  {}, {}, Confidence::Unresolved);
                g.nextStep = "Add create_clock for it (--sdc); until then every crossing involving it is UNKNOWN, not safe.";
                add(std::move(g));
                report->markIncomplete("constraints", "no constraint for clock '" + dom.name + "'");
            }
        }
    }

    // ------------------------------------------------------------ RDC-001 / CST-001
    void analyseResets() {
        evaluated["RDC-001"] = m->flops.size();
        evaluated["CST-001"] = m->flops.size();
        struct Rs { Root root; bool activeLow = false; std::set<uint32_t> domains; uint64_t regs = 0; SourceLocation loc; };
        std::map<NodeId, Rs> resets;
        uint64_t asyncRegs = 0, syncCand = 0, none = 0;
        std::map<uint32_t, std::array<uint64_t, 3>> perDomain;   // async, sync-candidate, none
        for (auto& f : m->flops) {
            uint32_t dm = f.clockResolved ? f.domain : ~0u;
            if (!f.resets.empty()) {
                asyncRegs++;
                if (dm != ~0u) perDomain[dm][0]++;
                for (auto& r : f.resets) {
                    auto& e = resets[r.root.node];
                    e.root = r.root;
                    e.activeLow = r.activeLow;
                    e.regs++;
                    if (f.clockResolved) e.domains.insert(f.domain);
                    if (!e.loc.valid()) e.loc = procOf(f).loc;
                }
            } else if (f.hasSyncResetCandidate) {
                syncCand++;
                if (dm != ~0u) perDomain[dm][1]++;
            } else {
                none++;
                if (dm != ~0u) perDomain[dm][2]++;
            }
        }
        for (auto& [node, r] : resets) {
            const char* kind = r.root.kind == RootKind::TopPort ? "top-level port" : r.root.kind == RootKind::Logic ? "logic-generated"
                             : r.root.kind == RootKind::BlackBox ? "black-box output" : r.root.kind == RootKind::Cycle ? "cyclic" : "undriven";
            std::string doms;
            for (auto dmn : r.domains) doms += (doms.empty() ? "" : ", ") + m->domains[dmn].name;
            Diagnostic d = mk("RDC-001", Severity::Info,
                              "reset source '" + m->nameOf(node) + "' (" + kind + ", asynchronous, " +
                              (r.activeLow ? "active-low" : "active-high") + " by edge polarity): " +
                              std::to_string(r.regs) + " register bit(s) in clock domain(s) [" + doms + "]",
                              r.loc, {}, Confidence::Informational);
            d.evidence.push_back({"reset_polarity", "polarity inferred from the event edge (negedge=active-low); assumes the block resets when the signal is asserted", r.loc});
            add(std::move(d));
            if (r.root.kind != RootKind::TopPort) {
                Diagnostic g = mk("RDC-001", Severity::Warning,
                                  "reset '" + m->nameOf(node) + "' is " + kind + ", not a primary input: its source and release synchronisation are unresolved",
                                  r.loc, {}, Confidence::Unresolved);
                g.nextStep = "Check the reset generation/synchroniser; RDC analysis of this reset is incomplete.";
                add(std::move(g));
                report->markIncomplete("reset-extraction", "reset '" + m->nameOf(node) + "' is " + kind);
            }
        }
        for (auto& [dm, c] : perDomain) {
            add(mk("CST-001", Severity::Info,
                   "clock domain '" + m->domains[dm].name + "': " + std::to_string(c[0]) + " register bit(s) with asynchronous reset, " +
                   std::to_string(c[1]) + " with a probable synchronous reset (heuristic), " + std::to_string(c[2]) +
                   " with no reset (classified NO_RESET_INFERRED; not declared by the user)",
                   {}, {}, Confidence::Heuristic));
        }
        (void)asyncRegs; (void)syncCand; (void)none;
        // sequential elements whose clock is unknown are also unclassified for CST-001
        uint64_t unk = 0;
        for (auto& f : m->flops) if (!f.clockResolved) unk++;
        if (unk) {
            add(mk("CST-001", Severity::Warning, std::to_string(unk) + " register bit(s) have no identified clock (see CDC-001)",
                   {}, {}, Confidence::Unresolved));
            report->markIncomplete("clock-extraction", std::to_string(unk) + " register bit(s) without an identified clock");
        }
    }

    // ------------------------------------------------------------ crossings
    // forward closure over exact (alias) combinational edges
    void aliasForward(NodeId start, std::vector<NodeId>& closure) const {
        std::unordered_set<NodeId> seen{start};
        closure.push_back(start);
        for (size_t i = 0; i < closure.size(); i++)
            for (auto ei : m->out[closure[i]]) {
                auto& e = m->edges[ei];
                if (e.type == EdgeType::Comb && e.exact && seen.insert(e.to).second) closure.push_back(e.to);
            }
    }

    Chain chainFor(uint32_t flopIdx) const {
        Chain ch;
        ch.stages.push_back(flopIdx);
        std::set<uint32_t> visited{flopIdx};
        uint32_t cur = flopIdx;
        bool first = true;
        for (int guard = 0; guard < 16; guard++) {
            const Flop& f = m->flops[cur];
            std::vector<NodeId> closure;
            aliasForward(f.q, closure);
            std::unordered_set<NodeId> cl(closure.begin(), closure.end());
            int logicConsumers = 0;
            std::map<NodeId, std::vector<const Edge*>> seqTargets;
            for (auto n : closure) {
                if (n < m->topOutput.size() && m->topOutput[n]) logicConsumers++;   // observable at a top-level port
                for (auto ei : m->out[n]) {
                    auto& e = m->edges[ei];
                    if (e.type == EdgeType::Comb && !e.exact) logicConsumers++;
                    else if (e.type == EdgeType::Seq) seqTargets[e.to].push_back(&e);
                }
            }
            // candidate next stage: a flop in the same domain whose only data source is this stage's output
            const Flop* next = nullptr;
            uint32_t nextIdx = 0;
            bool nextLogic = false;
            int otherConsumers = logicConsumers;
            for (auto& [tq, es] : seqTargets) {
                auto it = m->flopOfQ.find(tq);
                if (it == m->flopOfQ.end()) { otherConsumers++; continue; }
                const Flop& t = m->flops[it->second];
                bool sole = true;
                for (auto ei : m->in[tq]) {
                    auto& e = m->edges[ei];
                    if (e.type != EdgeType::Seq) continue;
                    if (t.syncReset != kNoNode && e.from == t.syncReset) continue;
                    if (!cl.count(e.from)) sole = false;
                }
                bool sameDomain = t.clockResolved && f.clockResolved && t.domain == f.domain && !t.blocking && !f.blocking;
                if (sole && sameDomain && !next && !visited.count(it->second)) {
                    next = &t; nextIdx = it->second;
                    for (auto* e : es) if (!e->exact) nextLogic = true;
                } else otherConsumers++;
            }
            if (first && otherConsumers > 0 && next) ch.extraFanout = true;
            if (first && !next) ch.extraFanout = otherConsumers > 0;
            if (!next) break;
            if (first && nextLogic) { ch.linkLogic = true; break; }
            first = false;
            ch.depth++;
            ch.stages.push_back(nextIdx);
            visited.insert(nextIdx);
            cur = nextIdx;
        }
        return ch;
    }

    struct Src { NodeId q; bool logic; };
    struct Cone { std::vector<Src> flops; std::vector<Src> inputs; std::vector<NodeId> bbLeaves; std::vector<NodeId> unresolved; std::vector<NodeId> undriven; };

    Cone coneOf(const Flop& d, std::vector<uint32_t>& stamp0, std::vector<uint32_t>& stamp1, uint32_t gen) const {
        Cone c;
        std::vector<std::pair<NodeId, bool>> st;
        for (auto ei : m->in[d.q]) {
            auto& e = m->edges[ei];
            if (e.type != EdgeType::Seq) continue;   // (a synchronous reset is an ordinary data source here)
            st.push_back({e.from, !e.exact});
        }
        while (!st.empty()) {
            auto [n, logic] = st.back();
            st.pop_back();
            auto& stamp = logic ? stamp1 : stamp0;
            if (stamp[n] == gen) continue;
            stamp[n] = gen;
            if (n == d.q) continue;
            if (m->flopOfQ.count(n)) { c.flops.push_back({n, logic}); continue; }
            if (n < m->topInput.size() && m->topInput[n]) { c.inputs.push_back({n, logic}); continue; }
            // A net touching a black box may be driven by it (or only read by it): record it, but keep tracing
            // the other drivers so that no crossing is lost.
            if (n < m->blackBoxDriven.size() && m->blackBoxDriven[n]) c.bbLeaves.push_back(n);
            if (n < m->unresolvedDriven.size() && m->unresolvedDriven[n]) c.unresolved.push_back(n);
            if (m->in[n].empty() && !(n < m->blackBoxDriven.size() && m->blackBoxDriven[n])) c.undriven.push_back(n);
            for (auto ei : m->in[n]) {
                auto& e = m->edges[ei];
                if (e.type == EdgeType::Comb || e.type == EdgeType::Seq) st.push_back({e.from, logic || !e.exact});
            }
        }
        return c;
    }

    struct Group {
        std::string srcName, dstName;
        uint32_t srcDomain = 0, dstDomain = 0;
        std::vector<uint32_t> dstFlops;
        std::set<NodeId> srcBits;
        bool anyLogicBefore = false;
        bool collapsed = false;       // tracked at whole-symbol granularity: no per-bit classification possible
        uint64_t realWidth = 0;
        SourceLocation loc;
        std::string hier;
    };

    void analyseCrossings() {
        evaluated["CDC-003"] = evaluated["CDC-004"] = evaluated["CDC-005"] = evaluated["CDC-006"] = 0;
        evaluated["CDC-007"] = evaluated["CDC-008"] = evaluated["CDC-009"] = evaluated["CDC-029"] = evaluated["CST-004"] = 0;
        int minStages = 2;
        try { minStages = std::max(2, std::stoi(cfg->option("CDC-005.min_stages", "2"))); } catch (...) {}

        std::vector<uint32_t> s0(m->nodes.size(), 0), s1(m->nodes.size(), 0);
        uint32_t gen = 0;
        std::map<std::pair<std::string, std::string>, Group> groups;
        std::map<std::pair<NodeId, uint32_t>, std::vector<uint32_t>> inputsToDomain;   // (input node, dst domain) -> dst flops
        std::set<std::pair<uint32_t, uint32_t>> unknownPairs;
        std::map<std::pair<uint32_t, uint32_t>, uint64_t> unknownPairCount;
        uint64_t unresolvedEnds = 0;
        std::map<std::string, std::vector<uint32_t>> bbCaptures, unresolvedCaptures, undrivenCaptures;
        std::vector<Chain> chains(m->flops.size());
        std::vector<char> haveChain(m->flops.size(), 0);

        for (uint32_t di = 0; di < m->flops.size(); di++) {
            const Flop& d = m->flops[di];
            gen++;
            Cone cone = coneOf(d, s0, s1, gen);
            for (auto& s : cone.flops) {
                const Flop& sf = m->flops[m->flopOfQ.at(s.q)];
                if (!sf.clockResolved || !d.clockResolved) { unresolvedEnds++; continue; }
                if (sf.domain == d.domain) continue;
                Relation rel = relation(sf.domain, d.domain);
                if (rel == Relation::Same) continue;
                if (rel == Relation::Unknown) { auto key = std::minmax(sf.domain, d.domain); unknownPairs.insert(key); unknownPairCount[key]++; }
                auto key = std::make_pair(stripBit(m->nameOf(s.q)), stripBit(m->nameOf(d.q)));
                auto& g = groups[key];
                g.srcName = key.first; g.dstName = key.second;
                g.srcDomain = sf.domain; g.dstDomain = d.domain;
                if (std::find(g.dstFlops.begin(), g.dstFlops.end(), di) == g.dstFlops.end()) g.dstFlops.push_back(di);
                g.srcBits.insert(s.q);
                if (m->collapsed(s.q) || m->collapsed(d.q)) {
                    g.collapsed = true;
                    g.realWidth = std::max<uint64_t>(g.realWidth, std::max(m->symbolWidth(d.q), m->symbolWidth(s.q)));
                }
                g.anyLogicBefore = g.anyLogicBefore || s.logic;
                if (!g.loc.valid()) { g.loc = procOf(d).loc; g.hier = procOf(d).instancePath; }
            }
            for (auto& in : cone.inputs) {
                if (!d.clockResolved) continue;
                inputsToDomain[{in.q, d.domain}].push_back(di);
            }
            std::string dstSym = stripBit(m->nameOf(d.q));
            if (!cone.bbLeaves.empty()) bbCaptures[dstSym].push_back(di);
            if (!cone.unresolved.empty()) unresolvedCaptures[dstSym].push_back(di);
            if (!cone.undriven.empty()) undrivenCaptures[dstSym].push_back(di);
        }

        // ---- per signal-level crossing groups
        for (auto& [key, g] : groups) {
            Relation rel = relation(g.srcDomain, g.dstDomain);
            std::sort(g.dstFlops.begin(), g.dstFlops.end());
            uint64_t width = g.dstFlops.size();
            evaluated["CDC-003"] += width;
            int unsynced = 0, shortChain = 0, logicLink = 0, fanout = 0, synced = 0;
            int minDepth = 99;
            for (auto di : g.dstFlops) {
                if (!haveChain[di]) { chains[di] = chainFor(di); haveChain[di] = 1; }
                const Chain& ch = chains[di];
                if (ch.linkLogic) logicLink++;
                else if (ch.depth < 2) unsynced++;
                else if (ch.depth < minStages) shortChain++;
                else synced++;
                if (ch.extraFanout) fanout++;
                if (!ch.linkLogic && ch.depth >= 2) minDepth = std::min(minDepth, ch.depth);
            }
            bool exclusive = rel == Relation::Exclusive;
            Severity sevViol = exclusive ? Severity::Info : rel == Relation::Async ? Severity::Error : Severity::Warning;
            Confidence conf = rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established;
            const Flop& f0 = m->flops[g.dstFlops[0]];
            if (g.collapsed) {
                // Per-bit precision was given up (wide symbol): nothing structural can be concluded, in particular
                // never "recognised synchroniser". Report as an unresolved multi-bit crossing.
                Diagnostic d = mk("CDC-004", exclusive ? Severity::Info : Severity::Warning,
                                  "clock-domain crossing " + g.srcName + " -> " + g.dstName + " involves a " +
                                  std::to_string(g.realWidth) + "-bit symbol tracked at whole-symbol granularity: synchroniser and "
                                  "protocol structure cannot be classified [clock relationship " + relName(rel) + "]",
                                  g.loc, g.hier, Confidence::Unresolved);
                d.signals = {g.srcName, g.dstName};
                d.evidence.push_back({"launch_register", g.srcName + " (clock domain " + m->domains[g.srcDomain].name + ")", {}});
                d.evidence.push_back({"capture_register", g.dstName + " (clock domain " + m->domains[g.dstDomain].name + ")", procOf(f0).loc});
                d.nextStep = "Split the bus into narrower signals or review the crossing manually.";
                add(std::move(d));
                report->markIncomplete("graph-precision", "wide crossing " + g.srcName + " -> " + g.dstName + " not classified");
                continue;
            }
            auto base = [&](const char* rule, Severity sev, std::string msg) {
                Diagnostic d = mk(rule, sev, std::move(msg), g.loc, g.hier, sev == Severity::Info ? Confidence::Informational : conf);
                d.signals = {g.srcName, g.dstName};
                d.evidence.push_back({"launch_register", g.srcName + " (clock domain " + m->domains[g.srcDomain].name + ")", {}});
                d.evidence.push_back({"capture_register", g.dstName + " (clock domain " + m->domains[g.dstDomain].name + ", " +
                                      std::to_string(width) + " bit(s))", procOf(f0).loc});
                d.evidence.push_back({"domain_provenance", "relationship between the two clocks: " + std::string(relName(rel)) +
                                      "; SDC clocks '" + domainClockName(g.srcDomain) + "' -> '" + domainClockName(g.dstDomain) + "'", {}});
                d.evidence.push_back({"data_path", g.anyLogicBefore ? "combinational logic exists on the path from the launch register"
                                                                    : "direct register-to-register path (no logic)", {}});
                return d;
            };
            std::string tag = rel == Relation::Unknown ? " [clock relationship UNKNOWN: treated as potentially asynchronous]" : "";
            if (unsynced) {
                const char* rule = width > 1 ? "CDC-004" : "CDC-003";
                Diagnostic d = base(rule, sevViol,
                                    std::string(width > 1 ? "multi-bit" : "single-bit") + " clock-domain crossing " + g.srcName + " -> " +
                                    g.dstName + " has no recognised synchroniser (" + std::to_string(unsynced) + " of " +
                                    std::to_string(width) + " bit(s))" + tag);
                d.nextStep = width > 1 ? "Use a recognised multi-bit scheme (handshake, Gray FIFO pointer, or hold-stable payload with a synchronised enable) and state it in the constraints."
                                        : "Insert a two-flop synchroniser in the destination clock domain, or declare the crossing in constraints with a justification.";
                add(std::move(d));
            }
            if (shortChain) {
                Diagnostic d = base("CDC-005", sevViol, "synchroniser for " + g.srcName + " -> " + g.dstName + " has depth " +
                                    std::to_string(minDepth) + ", below the required " + std::to_string(minStages) + tag);
                d.nextStep = "Add stages or lower CDC-005.min_stages if the library's flops justify it.";
                add(std::move(d));
            }
            if (logicLink) {
                Diagnostic d = base("CDC-006", sevViol, "combinational logic between the first and second synchroniser stage of " +
                                    g.dstName + " (" + std::to_string(logicLink) + " bit(s))" + tag);
                d.nextStep = "Remove logic between synchroniser stages; place logic after the last stage.";
                add(std::move(d));
            }
            if (fanout && (synced || shortChain)) {
                Diagnostic d = base("CDC-007", sevViol, "first-stage output of the synchroniser " + g.dstName +
                                    " fans out to logic other than the next stage (" + std::to_string(fanout) + " bit(s))" + tag);
                d.nextStep = "Only the last synchroniser stage may feed functional logic.";
                add(std::move(d));
            }
            if (g.anyLogicBefore) {
                Diagnostic d = base("CDC-008", exclusive ? Severity::Info : Severity::Warning,
                                    "combinational logic between the launch register " + g.srcName + " and the capture " +
                                    g.dstName + ": glitches may be sampled" + tag);
                d.nextStep = "Register the signal in the source domain before crossing.";
                add(std::move(d));
                evaluated["CDC-008"] += width;
            }
            if (synced && width > 1) {
                Diagnostic d = base("CDC-009", exclusive ? Severity::Info : Severity::Warning,
                                    std::to_string(width) + " bits of " + g.srcName + " are synchronised independently into " + g.dstName +
                                    ": bit skew can capture an inconsistent value (coherency not provable structurally)" + tag);
                d.proofState = ProofState::NotAttempted;
                d.nextStep = "Prove the bus is Gray-coded / one-hot / held stable by a synchronised qualifier, or use a handshake or async FIFO. Proof obligation: single-bit-change or stability.";
                add(std::move(d));
                evaluated["CDC-009"] += width;
            }
            if (synced && width == 1 && !unsynced && !logicLink && !fanout) {
                Diagnostic d = base("CDC-003", Severity::Info,
                                    "recognised " + std::to_string(minDepth) + "-stage flop synchroniser for " + g.srcName + " -> " + g.dstName + tag);
                d.evidence.push_back({"trust_model", "structural recognition only: assumes first-stage metastability resolves within one destination "
                                      "cycle and the source signal is glitch-free and held long enough (CST-007); no MTBF or timing is analysed", {}});
                d.proofState = ProofState::NotAttempted;
                add(std::move(d));
            }
        }

        // ---- CDC-029: top-level inputs captured by registers
        for (auto& [k, dsts] : inputsToDomain) {
            auto [inNode, dom] = k;
            std::sort(dsts.begin(), dsts.end());
            dsts.erase(std::unique(dsts.begin(), dsts.end()), dsts.end());
            int synced = 0;
            for (auto di : dsts) {
                if (!haveChain[di]) { chains[di] = chainFor(di); haveChain[di] = 1; }
                if (!chains[di].linkLogic && chains[di].depth >= 2) synced++;
            }
            evaluated["CDC-029"] += dsts.size();
            const Flop& f0 = m->flops[dsts[0]];
            bool allSynced = synced == static_cast<int>(dsts.size());
            Diagnostic d = mk("CDC-029", allSynced ? Severity::Info : Severity::Warning,
                              "top-level input '" + stripBit(m->nameOf(inNode)) + "' is captured in clock domain '" + m->domains[dom].name + "' by " +
                              std::to_string(dsts.size()) + " register bit(s)" +
                              (allSynced ? " through a recognised synchroniser" : " without a recognised synchroniser") +
                              "; no input-timing/CDC contract relates it to that clock (set_input_delay is not interpreted)",
                              procOf(f0).loc, procOf(f0).instancePath, allSynced ? Confidence::Heuristic : Confidence::Unresolved);
            d.signals = {stripBit(m->nameOf(inNode))};
            d.evidence.push_back({"capture_register", flopName(f0), procOf(f0).loc});
            d.nextStep = "Declare the input as synchronous to a clock or as an asynchronous input requiring a synchroniser.";
            add(std::move(d));
        }

        // ---- sources that cannot be classified: never silent
        auto unresolvedSource = [&](const char* rule, std::map<std::string, std::vector<uint32_t>>& caps, const char* what, const char* gapScope) {
            for (auto& [sym, dis] : caps) {
                const Flop& f0 = m->flops[dis[0]];
                Diagnostic d = mk(rule, Severity::Warning,
                                  "register '" + sym + "' (" + std::to_string(dis.size()) + " bit(s)) captures a value that " + what +
                                  "; whether it crosses a clock domain is unresolved",
                                  procOf(f0).loc, procOf(f0).instancePath, Confidence::Unresolved);
                d.evidence.push_back({"capture_register", sym, procOf(f0).loc});
                d.nextStep = "Model the source (provide the module, remove the unsupported construct) or review manually.";
                add(std::move(d));
                report->markIncomplete(gapScope, sym + ": " + what);
            }
        };
        unresolvedSource("CST-005", bbCaptures, "may originate from an unmodeled black box", "blackboxes");
        unresolvedSource("CST-010", unresolvedCaptures, "depends on logic whose dependencies were truncated by a precision fallback", "graph-precision");
        unresolvedSource("CST-010", undrivenCaptures, "has an undriven or unmodeled source", "graph-precision");

        // ---- CST-004: unknown relationships
        evaluated["CST-004"] = unknownPairs.size();
        for (auto& [a, b] : unknownPairs) {
            Diagnostic d = mk("CST-004", Severity::Warning,
                              "relationship between clock '" + m->domains[a].name + "' and '" + m->domains[b].name + "' is UNKNOWN; " +
                              std::to_string(unknownPairCount[{a, b}]) + " register-bit crossing(s) between them are unresolved, not safe",
                              {}, {}, Confidence::Unresolved);
            d.nextStep = "Declare the relationship (create_clock + set_clock_groups -asynchronous, or a common clock) with --sdc.";
            add(std::move(d));
            report->markIncomplete("clock-relationships", "unknown relationship '" + m->domains[a].name + "' <-> '" + m->domains[b].name + "'");
        }
        if (unresolvedEnds) {
            add(mk("CST-004", Severity::Warning,
                   std::to_string(unresolvedEnds) + " register-to-register path(s) involve a register with an unidentified clock; crossing status is unresolved",
                   {}, {}, Confidence::Unresolved));
            report->markIncomplete("clock-relationships", std::to_string(unresolvedEnds) + " path(s) with an unidentified clock");
        }
    }
};

class PassRule final : public Rule {
public:
    PassRule(std::string id, std::shared_ptr<Shared> sh, std::string gap)
        : id_(std::move(id)), sh_(std::move(sh)), gap_(std::move(gap)) {}
    std::string_view id() const override { return id_; }
    RuleState completeness() const override { return gap_.empty() ? RuleState::Executed : RuleState::Partial; }
    std::string partialReason() const override { return gap_; }
    void run(RuleContext& ctx) override {
        sh_->ensure(ctx);
        sh_->flush(id_, ctx);
    }

private:
    std::string id_;
    std::shared_ptr<Shared> sh_;
    std::string gap_;
};

} // namespace

std::vector<std::unique_ptr<Rule>> makeCdcRules() {
    auto sh = std::make_shared<Shared>();
    std::vector<std::unique_ptr<Rule>> v;
    auto add = [&](const char* id, const char* gap) { v.push_back(std::make_unique<PassRule>(id, sh, gap)); };
    add("CDC-001", "register clocks only: latches, instantiated library flops/macros and clock-gating cells are not classified; clock uncertainty when a block has several edge events");
    add("CDC-002", "generated/gated/muxed clocks are detected structurally (logic-driven root) but no relationship (divide ratio, phase) is derived or checked against create_generated_clock");
    add("CDC-003", "single-bit register-to-register crossings via a structural 2-flop chain only; no pulse/handshake/Gray recognition, no depth/enable analysis beyond fanout");
    add("CDC-004", "multi-bit crossings are reported when unsynchronised; recognised multi-bit protocols (handshake, Gray FIFO pointers, qualified payload) are not yet recognised, so safe protocols are still flagged");
    add("CDC-005", "stage counting follows exact register chains in one domain only; library synchroniser cells are not recognised");
    add("CDC-006", "only logic between stage 1 and 2 is checked");
    add("CDC-007", "first-stage fanout is checked through exact aliases and one level of logic consumers");
    add("CDC-008", "any combinational logic between launch register and capture register is flagged; glitch-freedom (e.g. one-hot/Gray source) is not analysed");
    add("CDC-009", "reports every independently synchronised multi-bit group; Gray/one-hot/stable-qualifier exemptions need proof obligations (not attempted)");
    add("CDC-029", "set_input_delay and other input constraints are not interpreted, so synchronous inputs are also flagged as unresolved");
    add("CST-001", "reset classification from event lists and a heuristic synchronous-reset pattern; explicit user-declared no_reset is not supported yet");
    add("CST-002", "only create_clock source objects are resolved");
    add("CST-004", "relationship derived from SDC clock names and set_clock_groups only; generated-clock and exception semantics are not applied");
    add("CST-005", "only modules that slang reports as unknown are inventoried; encrypted/blackbox-attributed modules are not distinguished");
    add("CST-009", "");
    add("CST-010", "records only the fallbacks produced by the connectivity/clock/reset model and in-source slang pragmas; constructs the model ignores without a fallback (classes, tasks, initial blocks, primitives) are not enumerated");
    add("CST-011", "clock constraints only; reset and I/O constraint coverage not evaluated");
    add("CST-018", "parse problems and unreadable files only; semantic validation of option values is limited");
    add("RDC-001", "asynchronous resets from event lists; synchronous resets are only a heuristic count; reset synchroniser structures are not yet analysed");
    return v;
}

} // namespace sverity
