#pragma once
// Shared CDC / reset / constraint analysis over the design model (RFC section 2: "shared analysis pass").
// One lazily-built analysis feeds all rule objects of this family; no rule re-parses RTL.
//
// Honesty policy implemented here:
//  * unknown clock relationship          -> WARNING, confidence "unresolved" (never "safe")
//  * recognised 2-flop synchroniser      -> INFO with the trust-model assumptions spelled out
//  * heuristic classifications           -> confidence "heuristic"
//  * every graph-precision fallback      -> CST-010 WARNING + scope INCOMPLETE
// The class is declared here; the crossing/protocol analyses live in cdc_pass.cpp (core) and cdc_protocol.cpp.
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>
#include <optional>

#include "domains/cone.h"
#include "domains/model.h"
#include "domains/relations.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "rules/rules.h"
#include "sverity/waiver.h"   // globMatch

namespace sverity {
namespace cdc {

using namespace model;

struct Chain {
    int depth = 1;
    bool linkLogic = false;      // logic between stage 1 and stage 2
    bool extraFanout = false;    // stage-1 output also feeds something other than stage 2
    bool edgeDetector = false;   // some stage is combined by logic with its delayed copy (toggle receiver)
    bool midFanout = false;      // an intermediate stage (2..n-1) also feeds something other than the next stage
    bool leaksDomain = false;    // a non-final stage output is captured by a register in another clock domain
    bool clockMismatch = false;  // a stage is clocked on a different edge than the previous one
    bool resetMismatch = false;  // a stage has different asynchronous resets than the previous one
    bool feedbackWithFanout = false; // feedback already reported as first-stage fanout
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
    Design* design_ = nullptr;

    // ---- protocol / structure recognition, defined in cdc_protocol.cpp
    struct SyncOut { uint32_t srcDomain = 0, dstDomain = 0; std::string hier, srcName; NodeId srcQ = kNoNode; uint64_t width = 0; };
    std::unordered_set<const slang::ast::ValueSymbol*> grayNets_;   // nets assigned b ^ (b >> 1) (binary-to-Gray)
    std::unordered_set<const slang::ast::ValueSymbol*> opaqueFunctionNets_;
    std::map<NodeId, SyncOut> syncedOut_;                           // Q of every stage at the required depth of a recognised synchroniser
    void findGrayNets();
    bool isGrayFlop(NodeId q) const;
    bool hasOpaqueFunction(NodeId q) const;
    NodeId aliasRoot(NodeId n) const { return m->aliasRoot(n); }
    /// True when `d` is enabled/selected by a control that is the output of a recognised synchroniser whose source
    /// is in `srcDomain` (the qualifier pattern: payload captured under a synchronised valid).
    enum class Qualification { ProvenNecessary, Disproven, Undecided };
    struct QualifierResult { Qualification state; std::string reason; };
    QualifierResult ctrlFromSynced(const Flop& d, uint32_t srcDomain) const;
    bool hasCtrlEdge(const Flop& d) const;
    bool counterLike(const std::set<NodeId>& srcBits);
    /// SDC path exceptions of `kind` whose from/to refs match the two nodes (at least one side must be named).
    std::vector<const sdc::PathException*> exceptionsFor(NodeId src, NodeId dst, bool anyKind,
                                                         sdc::PathException::Kind kind) const;

    void ensure(RuleContext& ctx) {
        if (built) return;
        built = true;
        cfg = &ctx.config;
        report = &ctx.report;
        for (const char* sc : {"clock-extraction", "reset-extraction", "clock-relationships", "constraints",
                               "graph-precision", "blackboxes"})
            report->declareScope(sc);
        design_ = &ctx.design;
        m = &getModel(ctx.design);
        cones_ = &model::sharedCones(*m);
        findGrayNets();
        report->setObjectCount("instances", m->numInstances);
        report->setObjectCount("procedures_analysed", m->numProcedures);
        report->setObjectCount("sequential_procedures", m->numSeqProcedures);
        report->setObjectCount("register_bits", m->flops.size());
        report->setObjectCount("clock_domains", m->domains.size());
        report->setObjectCount("graph_nodes", m->nodes.size());
        report->setObjectCount("graph_edges", m->edges.size());
        report->setObjectCount("blackbox_instances", m->blackBoxes.size());
        report->setObjectCount("precision_fallbacks", m->fallbacks.size());
        report->setObjectCount("subroutine_summaries", m->subroutineSummaries);
        report->setObjectCount("subroutine_body_nodes", m->subroutineBodyNodes);
        mapConstraints();
        analyseFallbacks();
        analyseBlackBoxes();
        analyseConstraints();
        analyseClocks();
        analyseResets();
        analyseCrossings();
        analyseOutputs();
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
    bool bitMatches(const std::string& kind, const std::string& pattern, NodeId n) const { return rel_->bitMatches(kind, pattern, n); }
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
            d.identityKey = cat;
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
                              "' (black box, UNMODELED): its clock/reset/CDC behaviour is not analysed" + std::string(v[0]->declared ? " (declared in a --library file: port directions and roles are known, behaviour is not)" : ""),
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
        // Validate every reference before applying an output contract.
        auto problem = [&](const std::string& message, const SourceLocation& loc) {
            evaluated["CST-018"]++;
            add(mk("CST-018", Severity::Warning, message, loc, {}, Confidence::Unresolved));
            report->markIncomplete("constraints", message);
        };
        auto resolve = [&](const std::vector<sdc::ObjectRef>& refs, bool output, const SourceLocation& loc, bool bits = false) {
            bool valid = true;
            for (const auto& ref : refs) {
                bool hit = false;
                for (NodeId n = 0; n < m->nodes.size() && !hit; ++n) {
                    if (output && !m->topOutput[n]) continue;
                    const auto kind = ref.kind.empty() ? "get_ports" : ref.kind;
                    hit = nameMatches(kind, ref.pattern, n, stripBit(m->nameOf(n))) || (bits && bitMatches(kind, ref.pattern, n));
                }
                if (!hit) valid = false;
                if (!hit) problem("constraint object '" + ref.pattern + "' resolves to no " +
                                  (output ? "top-level output port" : "supported design object") +
                                  "; entire declaration is NOT applied", loc);
            }
            return valid;
        };
        auto unapplied = [&](const char* command, const std::string& text, const SourceLocation& loc,
                             const char* reason) {
            evaluated["CST-009"]++;
            auto d = mk("CST-009", Severity::Warning, std::string("SDC command '") + command +
                        "' is parsed and recorded but NOT applied: " + reason, loc, {}, Confidence::Unresolved);
            d.evidence.push_back({"sdc_command", text, loc});
            d.nextStep = "Review this assumption manually; the declaration does not establish safety.";
            add(std::move(d));
            report->markIncomplete("constraints", std::string("unapplied SDC command '") + command + "'");
        };
        for (const auto& d : c.outputDelays) {
            bool valid = resolve(d.ports, true, d.loc);
            bool clock = false;
            for (const auto& ck : c.clocks)
                clock |= (ck.name.empty() && !ck.sources.empty() ? ck.sources[0].pattern : ck.name) == d.clock;
            if (!clock) problem("set_output_delay: clock '" + d.clock + "' resolves to no declared clock", d.loc);
            if (valid && clock) outputContracts_.push_back(&d);
        }
        for (size_t i = 0; i < c.caseAnalyses.size(); ++i) {
            const auto& cse = c.caseAnalyses[i];
            resolve(cse.objects, false, cse.loc, true);
            const auto& result = design_->caseResults[i];
            if (!result.applied) {
                if (result.reason.find("conflicting") != std::string::npos) problem(result.reason, cse.loc);
                unapplied("set_case_analysis", cse.text, cse.loc, result.reason.c_str());
                continue;
            }
            evaluated["CST-014"]++;
            std::string fixed;
            for (const auto& name : result.names) { if (!fixed.empty()) fixed += ", "; fixed += name + "=" + std::to_string(cse.value); }
            auto d = mk("CST-014", Severity::Warning, "analysed mode " + fixed +
                        "; alternative value is NOT covered; selected model removed " + std::to_string(m->casePrunedBranches) +
                        " conditional branches and " + std::to_string(m->casePrunedExpressions) + " mux expressions (run totals); crossings in removed logic are not inventoried" +
                        (result.assumed ? "; nothing drives the target, so this value is an assumption the design does not establish" : ""), cse.loc, {}, Confidence::Unresolved);
            d.evidence.push_back({"sdc_command", cse.text, cse.loc});
            d.nextStep = "Run each alternative mode separately and review crossings in that run.";
            add(std::move(d));
            report->markIncomplete("constraints", "selected case-analysis mode does not cover alternatives");
            report->markIncomplete("case-analysis-modes", "alternative case-analysis values are not analysed");
        }
        if (!cfg->constraintsProvided) {
            report->markIncomplete("constraints", "no constraint file provided: clock relationships are unknown");
        }
        {   // a clock name defined twice (an SDC redefinition overrides): CST-018
            std::map<std::string, std::string> firstSrc;
            for (auto& ck : c.clocks) {
                if (ck.generated || ck.name.empty() || ck.sources.empty()) continue;
                auto [it, fresh] = firstSrc.emplace(ck.name, ck.sources[0].pattern);
                if (!fresh && it->second != ck.sources[0].pattern) {
                    add(mk("CST-018", Severity::Warning, "clock name '" + ck.name + "' is defined more than once for different sources ('" + it->second + "', '" + ck.sources[0].pattern + "')",
                           ck.loc, {}, Confidence::Unresolved));
                    report->markIncomplete("constraints", "clock name '" + ck.name + "' defined twice");
                }
            }
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

    std::vector<const sdc::OutputDelay*> outputContracts_;

    // Output cones stop at launch registers, including final synchroniser stages.
    // Synchronisation before that boundary says nothing about the external capture.
    void analyseOutputs() {
        evaluated["CST-012"] = 0;
        std::map<std::string, std::vector<NodeId>> outputs;
        for (NodeId n = 0; n < m->nodes.size(); ++n)
            if (m->topOutput[n]) outputs[stripBit(m->nameOf(n))].push_back(n);
        for (const auto& [name, bits] : outputs) {
            for (const auto* decl : outputContracts_) {
                std::vector<NodeId> matched;
                for (auto n : bits)
                    for (const auto& ref : decl->ports)
                        if (nameMatches(ref.kind.empty() ? "get_ports" : ref.kind, ref.pattern, n, name)) {
                            matched.push_back(n);
                            break;
                        }
                if (matched.empty()) continue;
                evaluated["CST-012"] += matched.size();
                auto cone = cones_->ofNodes(matched);
                bool uncertain = cone.flops.empty() || !cone.inputs.empty() || !cone.bbLeaves.empty() ||
                                 !cone.unresolved.empty() || !cone.uncertainCalls.empty() || !cone.undriven.empty();
                bool async = false, logic = false;
                std::set<uint32_t> domains;
                std::set<std::string> relations;
                auto d = mk("CST-012", Severity::Info, "", decl->loc);
                d.signals = {name};
                d.evidence.push_back({"sdc_command", decl->text, decl->loc});
                for (const auto& src : cone.flops) {
                    const auto& f = *m->flopAt(src.q);
                    logic = logic || src.logic;
                    // Every clock of a register with several sequential drivers counts: a known asynchronous one stays a
                    // violation whichever block comes first.
                    for (auto launchDomain : f.clockResolved ? domainsOf(f) : std::vector<uint32_t>{f.domain}) {
                        Relation r = f.clockResolved ? rel_->relationByNames(domainClockName(launchDomain), decl->clock) : Relation::Unknown;
                        async |= r == Relation::Async;
                        uncertain |= r != Relation::Same && r != Relation::Async;
                        if (f.clockResolved) domains.insert(launchDomain);
                        relations.insert(relName(r));
                        d.evidence.push_back({"launch_register", flopName(f) + ": " + relName(r), procOf(f).loc});
                    }
                }
                // Several launch domains may glitch/reconverge even when individually related to capture.
                uncertain |= domains.size() > 1;
                std::string detail;
                if (cone.flops.empty()) detail += "; no launching register established";
                if (!cone.inputs.empty()) detail += "; primary-input feed-through";
                if (domains.size() > 1) detail += "; mixed launch domains";
                if (!cone.bbLeaves.empty()) detail += "; unmodeled black-box driver";
                if (!cone.uncertainCalls.empty()) detail += "; opaque subroutine in cone";
                if (!cone.unresolved.empty() || !cone.undriven.empty()) detail += "; unresolved or undriven cone";
                d.severity = async ? Severity::Error : uncertain ? Severity::Warning : Severity::Info;
                d.confidence = uncertain ? Confidence::Unresolved : Confidence::Established;
                d.message = "top-level output '" + name + "' with set_output_delay -clock " + decl->clock +
                    (async ? ": launch domain is declared asynchronous to external capture (boundary crossing)" :
                     uncertain ? ": output-boundary contract unresolved" : ": launch/capture clock contract is consistent; physical timing is not proved") + detail;
                d.nextStep = "Review the external capture protocol and timing; an upstream synchroniser does not establish output-boundary safety.";
                d.identityKey = decl->clock;
                for (const auto& relationship : relations) d.identityKey += ":" + relationship;
                Crossing boundary;
                if (!cone.flops.empty()) {
                    const auto& launch = *m->flopAt(cone.flops[0].q);
                    boundary.launch = endpoint(launch, stripBit(flopName(launch)), launch.domain);
                }
                boundary.capture.signal = name;
                boundary.capture.sdcClock = decl->clock;
                boundary.capture.location = decl->loc;
                boundary.relationship = async ? "ASYNC" : uncertain ? "UNKNOWN" : "SAME";
                boundary.widthBits = matched.size();
                boundary.logicOnPath = logic;
                d.crossing = std::move(boundary);
                if (uncertain) report->markIncomplete("constraints", "unresolved output-boundary contract for '" + name + "'");
                add(std::move(d));
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
            d.identityKey = dom.name;
            d.proofState = ProofState::NotApplicable;
            if (!domainClockName(i).empty()) d.evidence.push_back({"domain_provenance", "constrained as SDC clock '" + domainClockName(i) + "'", {}});
            add(std::move(d));
            if (dom.kind != RootKind::TopPort) {
                Diagnostic g = mk("CDC-002", Severity::Warning,
                                  std::string("clock '") + dom.name + "' is not a primary input: it is " + kind +
                                  "; its relationship to other clocks is unresolved",
                                  {}, {}, Confidence::Unresolved);
                g.identityKey = dom.name;
                g.nextStep = "Declare it with create_generated_clock / clock groups, or model the clock-generation cell.";
                add(std::move(g));
                report->markIncomplete("clock-extraction", std::string("clock '") + dom.name + "' is " + kind);
            }
            if (domainClockName(i).empty()) {
                Diagnostic g = mk("CST-011", Severity::Warning,
                                  "clock '" + dom.name + "' (" + std::to_string(perDomain[i]) + " register bit(s)) has no clock constraint",
                                  {}, {}, Confidence::Unresolved);
                g.identityKey = dom.name;
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
        struct Rs { Root root; bool activeLow = false; std::set<uint32_t> domains; uint64_t regs = 0, clears = 0, sets = 0, other = 0; SourceLocation loc; };
        std::map<NodeId, Rs> resets;
        uint64_t asyncRegs = 0, syncCand = 0, none = 0;
        std::map<uint32_t, std::array<uint64_t, 3>> perDomain;   // async, sync-candidate, none
        for (auto& f : m->flops) {
            uint32_t dm = f.clockResolved ? f.domain : ~0u;
            if (!f.resets.empty()) {
                asyncRegs++;
                if (dm != ~0u) perDomain[dm][0]++;
                for (size_t ri = 0; ri < f.resets.size(); ri++) {
                    auto& r = f.resets[ri];
                    auto& e = resets[r.root.node];
                    e.root = r.root;
                    e.activeLow = r.activeLow;
                    e.regs++;
                    char rv = ri < f.resetValues.size() ? f.resetValues[ri] : '?';
                    (rv == '0' ? e.clears : rv == '1' ? e.sets : e.other)++;
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
            d.identityKey = "async:" + m->nameOf(node);
            d.evidence.push_back({"reset_polarity", "polarity inferred from the event edge (negedge=active-low); assumes the block resets when the signal is asserted", r.loc});
            d.evidence.push_back({"reset_values", "register bits cleared to 0: " + std::to_string(r.clears) + ", set to 1: " + std::to_string(r.sets) + ", other or unknown value: " + std::to_string(r.other), r.loc});
            add(std::move(d));
            if (r.root.kind != RootKind::TopPort) {
                Diagnostic g = mk("RDC-001", Severity::Warning,
                                  "reset '" + m->nameOf(node) + "' is " + kind + ", not a primary input: its source and release synchronisation are unresolved",
                                  r.loc, {}, Confidence::Unresolved);
                g.identityKey = "unresolved:" + m->nameOf(node);
                g.nextStep = "Check the reset generation/synchroniser; RDC analysis of this reset is incomplete.";
                add(std::move(g));
                report->markIncomplete("reset-extraction", "reset '" + m->nameOf(node) + "' is " + kind);
            }
        }
        {   // synchronous reset candidates (heuristic: top-level if on one signal, constant assignments), polarity from the condition shape
            struct Sr { bool low = false; uint64_t regs = 0; SourceLocation loc; };
            std::map<NodeId, Sr> syncResets;
            for (auto& f : m->flops)
                if (f.resets.empty() && f.hasSyncResetCandidate && f.syncReset != kNoNode) {
                    auto& e = syncResets[f.syncReset];
                    e.low = f.syncActiveLow; e.regs++;
                    if (!e.loc.valid()) e.loc = procOf(f).loc;
                }
            for (auto& [node, e] : syncResets) {
                Diagnostic d = mk("RDC-001", Severity::Info,
                                  "synchronous reset candidate '" + m->nameOf(node) + "' (" + std::string(e.low ? "active-low" : "active-high") + " by condition shape): " +
                                  std::to_string(e.regs) + " register bit(s) assigned constants when it is asserted",
                                  e.loc, {}, Confidence::Heuristic);
                d.identityKey = "sync:" + m->nameOf(node);
                d.evidence.push_back({"reset_polarity", "heuristic: a top-level if on this signal whose branch only assigns constants", e.loc});
                add(std::move(d));
            }
        }
        for (auto& [dm, c] : perDomain) {
            auto d = mk("CST-001", Severity::Info,
                   "clock domain '" + m->domains[dm].name + "': " + std::to_string(c[0]) + " register bit(s) with asynchronous reset, " +
                   std::to_string(c[1]) + " with a probable synchronous reset (heuristic), " + std::to_string(c[2]) +
                   " with no reset (classified NO_RESET_INFERRED; not declared by the user)",
                   {}, {}, Confidence::Heuristic);
            d.identityKey = m->domains[dm].name;
            add(std::move(d));
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

    // Feedback is relevant only to this capture's D cone. Stop at other
    // flops, so feedback belonging to a source-domain toggle stays there.
    bool hasFeedback(const Flop& flop) const {
        if (m->selfLogic.count(flop.q) || m->selfExact.count(flop.q)) return true;
        std::vector<NodeId> pending;
        for (auto ei : m->in[flop.q]) {
            const Edge& e = m->edges[ei];
            if (e.type == EdgeType::Seq && !e.ctrl) pending.push_back(e.from);
        }
        std::unordered_set<NodeId> seen;
        while (!pending.empty()) {
            auto node = pending.back();
            pending.pop_back();
            if (node == flop.q) return true;
            if (!seen.insert(node).second || m->flopOfQ.count(node)) continue;
            for (auto ei : m->in[node]) {
                const Edge& e = m->edges[ei];
                if (e.type == EdgeType::Comb && !e.ctrl) pending.push_back(e.from);
            }
        }
        return false;
    }

    // Preserve the existing first-stage fanout diagnostic when feedback is
    // routed through procedural combinational logic, while withholding the
    // chain from the recognised synchroniser inventory.
    bool proceduralFeedback(const Flop& flop) const {
        std::vector<std::pair<NodeId, bool>> pending;
        for (auto ei : m->in[flop.q]) {
            const auto& e = m->edges[ei];
            if (e.type == EdgeType::Seq && !e.ctrl) pending.emplace_back(e.from, false);
        }
        std::set<std::pair<NodeId, bool>> seen;
        while (!pending.empty()) {
            auto [node, procedural] = pending.back(); pending.pop_back();
            if (node == flop.q) { if (procedural) return true; else continue; }
            if (!seen.emplace(node, procedural).second || m->flopOfQ.count(node)) continue;
            for (auto ei : m->in[node]) {
                const auto& e = m->edges[ei];
                if (e.type == EdgeType::Comb && !e.ctrl)
                    pending.emplace_back(e.from, procedural || m->procs[e.proc].kind == ProcKind::Comb);
            }
        }
        return false;
    }

    int minStages_ = 2;
    Chain chainFor(uint32_t flopIdx) const {
        Chain ch;
        ch.depth = 1 + m->flops[flopIdx].extraStages;
        ch.stages.push_back(flopIdx);
        const Flop& firstStage = m->flops[flopIdx];
        if (!cones_->of(firstStage)->uncertainCalls.empty()) return ch;
        const bool feedback = hasFeedback(firstStage);
        const bool feedbackInComb = feedback && proceduralFeedback(firstStage);
        if (feedback && !feedbackInComb) return ch;
        // A gated first capture is not an ordinary synchroniser. Reset control
        // remains in the graph and is evaluated as a crossing independently.
        for (auto ei : m->in[firstStage.q]) {
            const Edge& e = m->edges[ei];
            if (e.type == EdgeType::Seq && e.ctrl && e.from != firstStage.syncReset) return ch;
        }
        std::set<uint32_t> visited{flopIdx};
        uint32_t cur = flopIdx;
        bool first = true;
        for (int guard = 0; guard < 16; guard++) {
            const Flop& f = m->flops[cur];
            std::vector<NodeId> closure;
            aliasForward(f.q, closure);
            std::unordered_set<NodeId> cl(closure.begin(), closure.end());
            int logicConsumers = 0;
            std::vector<NodeId> logicTargets;
            std::map<NodeId, std::vector<const Edge*>> seqTargets;
            for (auto n : closure) {
                if (n < m->topOutput.size() && m->topOutput[n]) logicConsumers++;   // observable at a top-level port
                for (auto& dm : m->domains) if (dm.root == n) logicConsumers++;     // used as a clock
                for (auto ei : m->out[n]) {
                    auto& e = m->edges[ei];
                    if (e.type == EdgeType::AsyncCtl) logicConsumers++;            // used as an asynchronous reset/set
                    else if (e.type == EdgeType::Comb && !e.exact) { logicConsumers++; logicTargets.push_back(e.to); }
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
                bool sole = cones_->of(t)->uncertainCalls.empty();
                for (auto ei : m->in[tq]) {
                    auto& e = m->edges[ei];
                    if (e.type != EdgeType::Seq) continue;
                    if (t.syncReset != kNoNode && e.from == t.syncReset) continue;
                    if (!cl.count(e.from)) sole = false;
                }
                // a register this stage only enables (`if (s2) held <= 1`) does not carry the value: it is not a stage
                sole = sole && std::any_of(es.begin(), es.end(), [](const Edge* e) { return !e->ctrl; });
                bool sameDomain = t.clockResolved && f.clockResolved && t.domain == f.domain && !t.blocking && !f.blocking;
                const bool logic = std::any_of(es.begin(), es.end(), [](const Edge* e) { return !e->exact; });
                // An exact continuation is the next stage wherever it comes in the source; a register fed through logic is then a tap.
                if (sole && sameDomain && !visited.count(it->second) && (!next || (nextLogic && !logic))) {
                    if (next) otherConsumers++;
                    next = &t; nextIdx = it->second; nextLogic = logic;
                } else otherConsumers++;
            }
            // Past two stages the chain is complete, and a register fed through logic is its consumer rather than a broken stage.
            if (next && nextLogic && ch.depth >= 2) break;
            if (next && !first) {
                // logic that combines this stage with the next one (stage XOR delayed copy) is an edge detector, not a tap
                for (auto lt : logicTargets) {
                    bool withNext = false;
                    for (auto ei : m->in[lt]) if (m->edges[ei].from == next->q) withNext = true;
                    if (withNext) ch.edgeDetector = true;
                }
            }
            // A library cell's output is already past its first stage, so a tap on it taps a later stage.
            if (first && otherConsumers > 0 && next) {
                if (f.extraStages == 0) ch.extraFanout = true;
                else if (ch.depth < minStages_) ch.midFanout = true;
            }
            if (first && !next) ch.extraFanout = otherConsumers > 0 && f.extraStages == 0;   // a library cell's output is its last stage
            // A stage past the required depth is an output of the synchroniser, whatever registers follow it.
            if (!first && next && otherConsumers > 0 && ch.depth < minStages_) ch.midFanout = true;
            if (next) {
                for (auto& [tq, es] : seqTargets) {
                    auto it = m->flopOfQ.find(tq);
                    if (it != m->flopOfQ.end() && m->flops[it->second].clockResolved && f.clockResolved &&
                        m->flops[it->second].domain != f.domain && ch.depth < minStages_) ch.leaksDomain = true;
                }
                if (ch.depth < minStages_) {   // past the required depth, the next register is not part of the synchroniser
                    if (next->edge != f.edge) ch.clockMismatch = true;
                    auto resetSet = [](const Flop& x) { std::set<NodeId> r; for (auto& rr : x.resets) r.insert(rr.root.node); return r; };
                    if (resetSet(*next) != resetSet(f)) ch.resetMismatch = true;
                }
            }
            if (!next) break;
            if (nextLogic) { ch.linkLogic = true; break; }   // logic between any two stages
            first = false;
            ch.depth += 1 + next->extraStages;
            ch.stages.push_back(nextIdx);
            visited.insert(nextIdx);
            cur = nextIdx;
        }
        if (feedbackInComb) {
            if (ch.extraFanout && ch.depth >= 2) ch.feedbackWithFanout = true;
            else { ch.depth = 1; ch.stages.resize(1); }
        }
        // A feedback condition from a later stage is not a reset merely
        // because the branch writes a constant; it changes the capture.
        if (firstStage.syncReset != kNoNode) {
            for (size_t i = 1; i < ch.stages.size(); ++i) {
                if (m->flops[ch.stages[i]].q == firstStage.syncReset) {
                    ch.depth = 1;
                    ch.stages.resize(1);
                    break;
                }
            }
        }
        return ch;
    }

    using Cone = model::Cone;
    model::ConeFinder* cones_ = nullptr;
    std::shared_ptr<const Cone> coneOf(const Flop& d) { return cones_->of(d); }

    struct Group {
        std::string srcName, dstName;
        uint32_t srcDomain = 0, dstDomain = 0;
        std::vector<uint32_t> dstFlops;
        std::set<NodeId> srcBits;
        bool anyLogicBefore = false;
        bool collapsed = false;       // tracked at whole-symbol granularity: no per-bit classification possible
        uint64_t realWidth = 0;
        bool gray = false, counter = false, qualified = false, memory = false;
        NodeId srcQ = kNoNode;        // first source bit (control signals are single-bit)
        SourceLocation loc;
        std::string hier;
    };

    // What a waiver of this crossing accepted: changing a clock, edge or relationship must stale it.
    std::string crossingIdentity(const Group& g) const {
        const Flop* launch = m->flopAt(g.srcQ);
        const Flop& capture = m->flops[g.dstFlops[0]];
        // The capture condition too: `if (!en)` accepts a different interval from `if (en)`.
        std::set<std::string> guards;
        for (auto di : g.dstFlops)   // every bit: each can have its own condition
            for (auto ei : m->in[m->flops[di].q]) {
                const Edge& e = m->edges[ei];
                if (e.type == EdgeType::Seq && e.ctrl)
                    guards.insert(m->nameOf(m->flops[di].q) + "<" + m->nameOf(e.from) + "/" + std::to_string(static_cast<int>(e.branch)) + std::to_string(e.inv));
            }
        std::string identity = m->domains[g.srcDomain].name + ":" + std::to_string(launch ? launch->edge : 0) + "->" +
                               m->domains[g.dstDomain].name + ":" + std::to_string(capture.edge) + ":" +
                               relName(relation(g.srcDomain, g.dstDomain));
        for (const auto& guard : guards) identity += ":" + guard;
        return identity;
    }

    std::map<std::pair<std::string, std::string>, Group> groups_;
    std::vector<Chain> chains_;
    std::vector<char> haveChain_;
    std::set<std::pair<NodeId, uint32_t>> inputLogic_;                 // (input bit, capture domain) with logic before the first register
    std::set<uint32_t> multiInputDst_;                                  // registers whose cone combines several primary inputs
    std::set<std::pair<uint32_t, uint32_t>> sameNameDomainPairs_;      // distinct clock roots that SDC declares to be one clock
    std::map<std::pair<std::string, std::string>, std::pair<NodeId, NodeId>> halfCycle_;   // rising<->falling-edge register paths
    void analyseStructure();   // cdc_protocol.cpp: the structure analyses below
    void reconvergence();      // CDC-010 / 011 / 036
    void clockStructure();     // CDC-019 / 020 / 030 / 031
    void edgeStructure();      // CDC-043
    void protocols();          // CDC-012 / 013 / 014 / 015 / 024 / 034 / 035 / 037 / 038 / 040 / 041 / 042 / 044
    void sequentialElements(); // CDC-001 latches
    void libraryCells();       // cdc_library.cpp: CDC-020 / 035 / 045 / 046 for declared library RAMs, clock gates and black boxes
    int domainOfClockNode(NodeId clk) const;
    double periodNs(uint32_t domain) const;

    CrossingEndpoint endpoint(const Flop& f, const std::string& signal, uint32_t domain) const {
        const char* edge = f.edge == 1 ? "posedge" : f.edge == 2 ? "negedge" : f.edge == 3 ? "both" : "unknown";
        return CrossingEndpoint{signal, m->domains[domain].name, domainClockName(domain), edge, m->procs[f.proc].loc};
    }
    static const char* relationshipName(Relation r) {
        return r == Relation::Same ? "SAME" : r == Relation::Async ? "ASYNC" : r == Relation::Exclusive ? "EXCLUSIVE" : "UNKNOWN";
    }
    Crossing crossingOf(const Group& g) const {
        const Flop& launch = m->flops[m->flopOfQ.at(g.srcQ)];
        return Crossing{endpoint(launch, g.srcName, g.srcDomain), endpoint(m->flops[g.dstFlops[0]], g.dstName, g.dstDomain),
                        relationshipName(relation(g.srcDomain, g.dstDomain)), g.dstFlops.size(), g.anyLogicBefore};
    }
    // The shortest path from one launch bit to one capture bit, in signal order; endpoints alone do not show where logic sits.
    std::vector<Evidence> pathWitness(const Group& g) const {
        constexpr size_t kMaxSteps = 32, kMaxVisited = 100000;
        for (auto di : g.dstFlops) {
            NodeId capture = m->flops[di].q;
            std::unordered_map<NodeId, uint32_t> edgeTo;   // node -> edge towards the capture
            std::vector<NodeId> queue;
            for (auto ei : m->in[capture])
                if (m->edges[ei].type == EdgeType::Seq && edgeTo.emplace(m->edges[ei].from, ei).second) queue.push_back(m->edges[ei].from);
            for (size_t i = 0; i < queue.size() && i < kMaxVisited; i++) {
                NodeId n = queue[i];
                if (g.srcBits.count(n)) {
                    std::vector<Evidence> steps;
                    for (NodeId cur = n; cur != capture; cur = m->edges[edgeTo.at(cur)].to) {
                        const Edge& e = m->edges[edgeTo.at(cur)];
                        if (steps.size() == kMaxSteps) { steps.push_back({"path_step", "... continues to " + m->nameOf(capture), {}}); break; }
                        steps.push_back({"path_step", m->nameOf(e.from) + " -> " + m->nameOf(e.to), m->procs[e.proc].loc});
                    }
                    return steps;
                }
                if (m->flopOfQ.count(n)) continue;   // a register ends the combinational cone
                for (auto ei : m->in[n])
                    if (m->edges[ei].type == EdgeType::Comb && edgeTo.emplace(m->edges[ei].from, ei).second) queue.push_back(m->edges[ei].from);
            }
        }
        return {};
    }

    // Several rules can describe one unsynchronised crossing. The most specific enabled rule is the finding; the others
    // become its evidence. A disabled rule never takes the crossing with it, so disabling one cannot hide the crossing, and an
    // absorbed rule keeps its own severity override and waiver, so accepting one description does not accept the others.
    void reportPrimary(std::vector<Diagnostic> facts) {
        if (facts.empty()) return;
        static const char* const order[] = {"CDC-017", "CDC-032", "CDC-028", "CDC-016", "CDC-004", "CDC-003"};
        auto rank = [](const Diagnostic& d) { return std::find_if(std::begin(order), std::end(order), [&](const char* id) { return d.ruleId == id; }) - std::begin(order); };
        std::stable_sort(facts.begin(), facts.end(), [&](const Diagnostic& a, const Diagnostic& b) { return rank(a) < rank(b); });
        auto chosen = std::find_if(facts.begin(), facts.end(), [&](const Diagnostic& d) { return !cfg->disabledRules.count(d.ruleId); });
        if (chosen == facts.end()) return;   // every description was disabled: coverage records that, CST-019 reports it
        Diagnostic primary = *chosen;
        for (auto& other : facts) {
            if (&other == &*chosen) continue;
            primary.severity = std::max(primary.severity, other.severity);
            // A heuristic description of an established violation does not weaken the violation.
            if (other.confidence == Confidence::Established) primary.confidence = Confidence::Established;
            primary.evidence.push_back({"also_matches", other.ruleId + ": " + other.message, {}});
            if (!cfg->disabledRules.count(other.ruleId)) {
                primary.alsoRules.push_back(other.ruleId);
                primary.identityKey += "+" + other.ruleId;
            }
        }
        add(std::move(primary));
    }

    void analyseCrossings() {
        evaluated["CDC-026"] = evaluated["CDC-032"] = 0;
        evaluated["CDC-016"] = evaluated["CDC-017"] = evaluated["CDC-018"] = evaluated["CDC-028"] = evaluated["CDC-039"] = 0;
        evaluated["CDC-003"] = evaluated["CDC-004"] = evaluated["CDC-005"] = evaluated["CDC-006"] = 0;
        evaluated["CDC-007"] = evaluated["CDC-008"] = evaluated["CDC-009"] = evaluated["CDC-029"] = evaluated["CST-004"] = 0;
        int minStages = 2;
        {
            std::string value = cfg->option("CDC-005.min_stages", "2");
            size_t end = 0;
            minStages = std::stoi(value, &end);
            if (end != value.size() || minStages < 2) throw std::invalid_argument("CDC-005.min_stages must be an integer >= 2");
        }
        minStages_ = minStages;

        auto& groups = groups_;
        std::map<std::pair<NodeId, uint32_t>, std::vector<uint32_t>> inputsToDomain;   // (input node, dst domain) -> dst flops
        std::set<std::pair<uint32_t, uint32_t>> unknownPairs;
        std::map<std::pair<uint32_t, uint32_t>, uint64_t> unknownPairCount;
        uint64_t unresolvedEnds = 0;
        std::map<std::string, std::vector<uint32_t>> bbCaptures, unresolvedCaptures, undrivenCaptures;
        std::map<std::pair<std::string, uint32_t>, std::vector<uint32_t>> callCaptures;
        std::map<std::string, std::set<std::string>> undrivenLeaves;
        chains_.assign(m->flops.size(), Chain{});
        haveChain_.assign(m->flops.size(), 0);
        auto& chains = chains_;
        auto& haveChain = haveChain_;

        // The cones hold billions of sources on a large design: names are built once per node, and a pair finds its group once.
        std::vector<uint32_t> nameIdOf(m->nodes.size(), UINT32_MAX);
        std::unordered_map<std::string, uint32_t> nameIds;
        std::vector<const std::string*> names;
        const auto nameId = [&](NodeId n) {
            auto& id = nameIdOf[n];
            if (id == UINT32_MAX) {
                auto [it, fresh] = nameIds.try_emplace(stripBit(m->nameOf(n)), static_cast<uint32_t>(names.size()));
                if (fresh) names.push_back(&it->first);
                id = it->second;
            }
            return id;
        };
        std::unordered_map<uint64_t, std::remove_reference_t<decltype(groups)>::mapped_type*> groupOf;

        for (uint32_t di = 0; di < m->flops.size(); di++) {
            const Flop& d = m->flops[di];
            const auto heldCone = coneOf(d);
            const Cone& cone = *heldCone;
            const uint32_t dstId = nameId(d.q);
            for (auto& s : cone.flops) {
                const Flop& sf = m->flops[m->flopOfQ.at(s.q)];
                if (!sf.clockResolved || !d.clockResolved) { unresolvedEnds++; continue; }
                const auto pair = worstDomains(sf, d, *rel_);
                if (pair.launch == pair.capture) {
                    if (sf.edge && d.edge && sf.edge != d.edge)
                        halfCycle_.emplace(std::make_pair(*names[nameId(s.q)], *names[dstId]), std::make_pair(s.q, d.q));
                    continue;
                }
                const Relation rel = pair.relation;
                if (rel == Relation::Same) { sameNameDomainPairs_.insert({pair.launch, pair.capture}); continue; }
                if (rel == Relation::Unknown) { auto key = std::minmax(pair.launch, pair.capture); unknownPairs.insert(key); unknownPairCount[key]++; }
                const uint32_t srcId = nameId(s.q);
                auto*& known = groupOf[uint64_t{srcId} << 32 | dstId];
                if (!known) {
                    known = &groups[std::make_pair(*names[srcId], *names[dstId])];
                    known->srcName = *names[srcId]; known->dstName = *names[dstId];
                }
                auto& g = *known;
                g.srcDomain = pair.launch; g.dstDomain = pair.capture;
                if (g.dstFlops.empty() || g.dstFlops.back() != di) g.dstFlops.push_back(di);   // registers are visited in order
                g.srcBits.insert(s.q);
                if (g.srcQ == kNoNode) g.srcQ = s.q;
                if (auto* sy = m->nodes[s.q].sym; sy && sy->getType().isUnpackedArray()) g.memory = true;
                if (auto* dy = m->nodes[d.q].sym; dy && dy->getType().isUnpackedArray()) g.memory = true;
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
                if (in.logic) inputLogic_.insert({in.q, d.domain});
                if (cone.inputs.size() >= 2) multiInputDst_.insert(di);
            }
            const std::string& dstSym = *names[dstId];
            for (auto issue : cone.uncertainCalls) callCaptures[{dstSym, issue}].push_back(di);
            if (!cone.bbLeaves.empty()) bbCaptures[dstSym].push_back(di);
            if (!cone.unresolved.empty()) unresolvedCaptures[dstSym].push_back(di);
            if (!cone.undriven.empty()) {
                undrivenCaptures[dstSym].push_back(di);
                for (auto leaf : cone.undriven) undrivenLeaves[dstSym].insert(stripBit(m->nameOf(leaf)));
            }
        }

        // ---- pre-pass: chains and the outputs of recognised synchronisers (needed to recognise qualifiers)
        for (auto& [key, g] : groups) {
            for (auto di : g.dstFlops) {
                if (!haveChain[di]) { chains[di] = chainFor(di); haveChain[di] = 1; }
                const Chain& ch = chains[di];
                if (!ch.linkLogic && !ch.feedbackWithFanout && ch.depth >= 2)
                {
                    // Every stage at the required depth is an output, not only the last: followers do not change what reads it.
                    int reached = 0;
                    for (size_t stage = 0; stage < ch.stages.size(); ++stage) {
                        const Flop& output = m->flops[ch.stages[stage]];
                        reached += 1 + output.extraStages;
                        if (reached >= minStages_ || stage + 1 == ch.stages.size())
                            syncedOut_[output.q] = {g.srcDomain, g.dstDomain, g.hier, g.srcName, g.srcQ, g.dstFlops.size()};
                    }
                }
            }
        }

        // ---- per signal-level crossing groups
        for (auto& [key, g] : groups) {
            Relation rel = relation(g.srcDomain, g.dstDomain);
            std::sort(g.dstFlops.begin(), g.dstFlops.end());
            uint64_t width = g.dstFlops.size();
            evaluated["CDC-003"] += width;
            int unsynced = 0, shortChain = 0, logicLink = 0, fanout = 0, synced = 0;
            int minDepth = 99, maxDepth = 0;
            for (auto di : g.dstFlops) {
                if (!haveChain[di]) { chains[di] = chainFor(di); haveChain[di] = 1; }
                const Chain& ch = chains[di];
                if (ch.linkLogic) logicLink++;
                else if (ch.depth < 2) unsynced++;
                else if (ch.depth < minStages) shortChain++;
                else synced++;
                if (ch.extraFanout) fanout++;
                if (!ch.linkLogic && ch.depth >= 2) { minDepth = std::min(minDepth, ch.depth); maxDepth = std::max(maxDepth, ch.depth); }
            }
            // protocol recognition: Gray-coded source, binary counter source, payload qualified by a synchronised control
            bool gray = false, counter = false, qualified = false, opaqueFunction = false;
            Qualification qualification = Qualification::Disproven;
            std::string qualifierReason;
            if (!g.collapsed) {
                for (auto q : g.srcBits) opaqueFunction = opaqueFunction || hasOpaqueFunction(q);
                gray = !opaqueFunction && !g.srcBits.empty();
                for (auto q : g.srcBits) gray = gray && isGrayFlop(q);
                if (gray) {   // a Gray pointer is the conversion of a counter: the binary side must be a carry-dependent register group
                    bool fromCounter = false;
                    for (auto q : g.srcBits) {
                        auto fit = m->flopOfQ.find(q);
                        if (fit == m->flopOfQ.end()) continue;
                        const auto heldCone = coneOf(m->flops[fit->second]);
                        const Cone& gc = *heldCone;
                        std::set<NodeId> leaves;
                        for (auto& x : gc.flops) leaves.insert(x.q);
                        if (!leaves.empty() && counterLike(leaves)) fromCounter = true;
                    }
                    gray = fromCounter;
                }
                counter = !gray && !opaqueFunction && g.srcBits.size() > 1 && counterLike(g.srcBits);
                qualification = Qualification::ProvenNecessary;
                for (auto di : g.dstFlops) {
                    auto result = ctrlFromSynced(m->flops[di], g.srcDomain);
                    if (chains[di].depth >= 2 || result.state == Qualification::Disproven) {
                        qualification = Qualification::Disproven;
                        break;
                    }
                    if (result.state == Qualification::Undecided) {
                        qualification = Qualification::Undecided;
                        qualifierReason = result.reason;
                    }
                }
                qualified = qualification == Qualification::ProvenNecessary;
            }
            g.gray = gray; g.counter = counter; g.qualified = qualified;
            bool exclusive = rel == Relation::Exclusive;
            Severity sevViol = exclusive ? Severity::Info : rel == Relation::Async ? Severity::Error : Severity::Warning;
            Confidence conf = rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established;
            const Flop& f0 = m->flops[g.dstFlops[0]];
            if (g.collapsed) {
                // Per-bit precision was given up (wide symbol): nothing structural can be concluded, in particular
                // never "recognised synchroniser". Report as an unresolved multi-bit crossing.
                bool noNextStage = true;   // the destination registers feed no further same-domain stage: nothing can be a synchroniser
                for (auto di : g.dstFlops) noNextStage = noNextStage && chains[di].depth < 2 && !chains[di].linkLogic;
                Diagnostic d = mk(g.memory ? "CDC-035" : "CDC-004",
                                  exclusive ? Severity::Info : (noNextStage && !g.memory && rel == Relation::Async) ? Severity::Error : Severity::Warning,
                                  "clock-domain crossing " + g.srcName + " -> " + g.dstName + " involves a " +
                                  std::to_string(g.realWidth) + "-bit symbol tracked at whole-symbol granularity: synchroniser and "
                                  "protocol structure cannot be classified [clock relationship " + relName(rel) + "]",
                                  g.loc, g.hier, Confidence::Unresolved);
                d.signals = {g.srcName, g.dstName};
                d.crossing = crossingOf(g);
                d.identityKey = crossingIdentity(g);
                d.evidence.push_back({"launch_register", g.srcName + " (clock domain " + m->domains[g.srcDomain].name + ")", {}});
                d.evidence.push_back({"capture_register", g.dstName + " (clock domain " + m->domains[g.dstDomain].name + ")", procOf(f0).loc});
                d.nextStep = "Split the bus into narrower signals or review the crossing manually.";
                add(std::move(d));
                report->markIncomplete("graph-precision", "wide crossing " + g.srcName + " -> " + g.dstName + " not classified");
                continue;
            }
            std::optional<std::vector<Evidence>> witness;   // computed once per group, for the findings it actually gets
            auto base = [&](const char* rule, Severity sev, std::string msg) {
                Diagnostic d = mk(rule, sev, std::move(msg), g.loc, g.hier, sev == Severity::Info ? Confidence::Informational : conf);
                const Flop& launch = m->flops[m->flopOfQ.at(g.srcQ)];
                d.crossing = crossingOf(g);
                d.signals = {g.srcName, g.dstName};
                d.identityKey = crossingIdentity(g);
                if (rel == Relation::Exclusive) d.evidence.push_back({"trust_model", "the two clocks are declared exclusive by constraint (trusted, not verified)", {}});
                d.evidence.push_back({"launch_register", g.srcName + " (clock domain " + m->domains[g.srcDomain].name + ")", procOf(launch).loc});
                d.evidence.push_back({"capture_register", g.dstName + " (clock domain " + m->domains[g.dstDomain].name + ", " +
                                      std::to_string(width) + " bit(s))", procOf(f0).loc});
                d.evidence.push_back({"domain_provenance", "relationship between the two clocks: " + std::string(relName(rel)) +
                                      "; SDC clocks '" + domainClockName(g.srcDomain) + "' -> '" + domainClockName(g.dstDomain) + "'", {}});
                d.evidence.push_back({"data_path", g.anyLogicBefore ? "combinational logic exists on the path from the launch register"
                                                                    : "direct register-to-register path (no logic)", {}});
                if (!witness) witness = pathWitness(g);
                d.evidence.insert(d.evidence.end(), witness->begin(), witness->end());
                return d;
            };
            std::string tag = rel == Relation::Unknown ? " [clock relationship UNKNOWN: treated as potentially asynchronous]" : "";
            std::vector<Diagnostic> busFacts;   // alternative descriptions of this one crossing: reported as one primary finding
            if (width > 1 && !g.collapsed) {
                evaluated["CDC-032"] += width;
                if (maxDepth && minDepth != maxDepth) {
                    Diagnostic d = base("CDC-032", sevViol, "bits of " + g.srcName + " -> " + g.dstName + " are synchronised through chains of different depth (" + std::to_string(minDepth) + " to " +
                                        std::to_string(maxDepth) + " stages): the bundle arrives with a fixed skew and is incoherent for a cycle" + tag);
                    d.nextStep = "Give every bit of a bundle the same synchroniser structure, or cross it with a qualifier/handshake.";
                    busFacts.push_back(std::move(d));
                } else if (synced && unsynced) {
                    Diagnostic d = base("CDC-032", sevViol, "part of the bundle " + g.srcName + " -> " + g.dstName + " is synchronised (" + std::to_string(synced) + " bits) and part is not (" +
                                        std::to_string(unsynced) + " bits): the bits are not captured coherently" + tag);
                    busFacts.push_back(std::move(d));
                }
            }
            if (unsynced && qualification == Qualification::Undecided) {
                Diagnostic d = base("CDC-039", Severity::Warning,
                                    "payload qualification is unresolved for " + g.srcName + " -> " + g.dstName + ": " + qualifierReason + tag);
                d.confidence = Confidence::Unresolved;
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"proof_obligation", "prove that every data-moving assignment requires control synchronized from the launch domain into the capture domain; " + qualifierReason, {}});
                d.nextStep = "Review the capture predicate and protocol state; separately establish data stability and timing.";
                add(std::move(d));
                evaluated["CDC-039"] += width;
                report->markIncomplete("payload-qualification", g.srcName + " -> " + g.dstName + ": " + qualifierReason);
            }
            if (qualified && unsynced) {
                Diagnostic d = base("CDC-039", Severity::Info,
                                    "multi-bit payload " + g.srcName + " -> " + g.dstName + " is captured under a control that is synchronised "
                                    "from the same source domain (qualifier pattern)" + tag);
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"proof_obligation", "the payload must be stable in the source domain from before the qualifier is launched "
                                      "until after the destination has captured it; no skew/stability constraint or protocol proof is checked here", {}});
                d.nextStep = "Hold the payload stable until the destination acknowledges, or use a handshake/async FIFO; constrain the data path (set_max_delay -datapath_only).";
                add(std::move(d));
                evaluated["CDC-039"] += width;
            }
            if (unsynced && qualification == Qualification::Disproven) {
                const char* rule = width > 1 ? "CDC-004" : "CDC-003";
                Diagnostic d = base(rule, sevViol,
                                    std::string(width > 1 ? "multi-bit" : "single-bit") + " clock-domain crossing " + g.srcName + " -> " +
                                    g.dstName + " has no recognised synchroniser (" + std::to_string(unsynced) + " of " +
                                    std::to_string(width) + " bit(s))" + tag);
                d.nextStep = width > 1 ? "Use a recognised multi-bit scheme (handshake, Gray FIFO pointer, or hold-stable payload with a synchronised enable) and state it in the constraints."
                                        : (rel == Relation::Unknown ? "Declare the clock intent and review the crossing; if asynchronous, add a suitable synchroniser or justify a waiver." : "Add a suitable synchroniser in the destination clock domain, or document and justify a waiver; a clock declaration does not repair the crossing.");
                busFacts.push_back(std::move(d));
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
                d.nextStep = "Only a stage at the required depth may feed functional logic.";
                add(std::move(d));
            }
            {
                int clkMis = 0, rstMis = 0, mid = 0, leak = 0;
                for (auto di : g.dstFlops) {
                    clkMis += chains[di].clockMismatch; rstMis += chains[di].resetMismatch;
                    mid += chains[di].midFanout; leak += chains[di].leaksDomain;
                }
                evaluated["CDC-026"] += width;
                if (clkMis || rstMis) {
                    Diagnostic d = base("CDC-026", sevViol, "synchroniser stages of " + g.dstName + " are not clocked/reset alike (" +
                                        std::string(clkMis ? "different clock edge" : "") + (clkMis && rstMis ? " and " : "") +
                                        (rstMis ? "different asynchronous reset" : "") + " between stages)" + tag);
                    d.nextStep = "Clock every stage on the same edge of the destination clock and give all stages the same reset.";
                    add(std::move(d));
                }
                if (mid || leak) {
                    Diagnostic d = base("CDC-007", sevViol, "an intermediate stage of the synchroniser " + g.dstName + " " +
                                        (leak ? "is captured by a register in another clock domain (feedback/leak)" : "feeds logic other than the next stage") + tag);
                    d.nextStep = "Only a stage at the required depth may be used; do not tap or feed back earlier stages.";
                    add(std::move(d));
                }
            }
            if (g.anyLogicBefore) {
                Diagnostic d = base("CDC-008", exclusive ? Severity::Info : Severity::Warning,
                                    "combinational logic between the launch register " + g.srcName + " and the capture " +
                                    g.dstName + ": glitches may be sampled" + tag);
                d.nextStep = "Register the signal in the source domain before crossing.";
                add(std::move(d));
                evaluated["CDC-008"] += width;
            }
            if (unsynced && qualification == Qualification::Disproven) {
                bool enabled = false;
                for (auto di : g.dstFlops) enabled = enabled || hasCtrlEdge(m->flops[di]);
                if (enabled) {
                    Diagnostic d = base("CDC-028", sevViol, "capture of " + g.srcName + " -> " + g.dstName +
                                        " is qualified by a clock enable that is not synchronised from the source domain: an enable in the "
                                        "destination domain only gates the flop, it does not synchronise the data" + tag);
                    d.nextStep = "Qualify the capture with a control synchronised from the source domain (and hold the data stable), or synchronise the data.";
                    busFacts.push_back(std::move(d));
                    evaluated["CDC-028"] += width;
                }
                // a synchronised control exists between the same domains in the same scope, but this payload is not gated by it
                if (width > 1) for (auto& [q, so] : syncedOut_)
                    if (so.srcDomain == g.srcDomain && so.dstDomain == g.dstDomain && so.hier == g.hier && so.srcName != g.srcName) {
                        Diagnostic d = base("CDC-016", sevViol, "payload " + g.srcName + " -> " + g.dstName + " crosses ungated although the control " +
                                            so.srcName + " is synchronised between the same clock domains" + tag);
                        d.nextStep = "Capture the payload only when the synchronised control says it is stable.";
                        busFacts.push_back(std::move(d));
                        evaluated["CDC-016"] += width;
                        break;
                    }
            }
            if (width > 1 && opaqueFunction) {
                evaluated["CDC-017"] += width;
                Diagnostic d = base("CDC-017", Severity::Warning,
                                    "pointer encoding of " + g.srcName + " -> " + g.dstName +
                                    " is unresolved: a user function producing the source value was not analysed" + tag);
                d.confidence = Confidence::Unresolved;
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"proof_obligation", "establish the pointer encoding and verify that at most one bit changes between sampled values", {}});
                d.nextStep = "Review the function and all writes to the pointer, and prove the required crossing protocol.";
                add(std::move(d));
                report->markIncomplete("cdc-017", "user-function output prevents classification of " + g.srcName);
            }
            if (width > 1 && (gray || counter)) {
                evaluated["CDC-017"] += width; evaluated["CDC-018"] += width;
                if (counter) {
                    Diagnostic d = base("CDC-017", sevViol, "multi-bit pointer " + g.srcName + " -> " + g.dstName + " is a binary counter: several bits change at "
                                        "once, so independently synchronised bits can be captured in an invalid state" + tag);
                    d.confidence = Confidence::Heuristic;
                    d.nextStep = "Convert the pointer to Gray code before the crossing (single bit changes per increment).";
                    busFacts.push_back(std::move(d));
                }
                if (gray) {
                    if (unsynced || shortChain || logicLink) {
                        Diagnostic d = base("CDC-017", sevViol, "Gray-coded pointer " + g.srcName + " -> " + g.dstName +
                                            " is not fully synchronised (" + std::to_string(unsynced + shortChain + logicLink) + " of " +
                                            std::to_string(width) + " bit(s) lack a clean multi-stage synchroniser)" + tag);
                        d.nextStep = "Every pointer bit needs its own synchroniser chain of the required depth with no logic between stages.";
                        busFacts.push_back(std::move(d));
                    }
                    if (synced == static_cast<int>(width)) {
                        NodeId sq = *g.srcBits.begin();
                        NodeId dq = m->flops[g.dstFlops[0]].q;
                        auto ex = exceptionsFor(sq, dq, false, sdc::PathException::Kind::MaxDelay);
                        if (ex.empty()) {
                            Diagnostic d = base("CDC-018", exclusive ? Severity::Info : Severity::Warning,
                                                "Gray-coded crossing " + g.srcName + " -> " + g.dstName + " has no bus-skew/max-delay constraint: "
                                                "the Gray property only holds if all bits arrive within one source clock period" + tag);
                            d.confidence = Confidence::Unresolved;
                            d.proofState = ProofState::NotAttempted;
                            d.evidence.push_back({"proof_obligation", "assumes the pointer changes by at most one count per source clock (Gray property) and that the source stays stable long enough: neither is proven", {}});
                            d.nextStep = "Add set_max_delay -datapath_only (or a bus skew constraint) on the pointer bits.";
                            add(std::move(d));
                        } else {
                            Diagnostic d = base("CDC-018", Severity::Info, "Gray-coded crossing " + g.srcName + " -> " + g.dstName +
                                                " has a max-delay constraint (" + ex[0]->loc.file + ":" + std::to_string(ex[0]->loc.line) +
                                                "); its value is not checked against the clock periods" + tag);
                            d.confidence = Confidence::Heuristic;
                            d.proofState = ProofState::NotAttempted;
                            d.evidence.push_back({"trust_model", "the max-delay constraint is trusted: its value against the clock periods and the bus skew are not verified", {}});
                            add(std::move(d));
                        }
                    }
                }
            } else if (synced && width > 1) {
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
            reportPrimary(std::move(busFacts));
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
            // set_input_delay -clock C declares the input synchronous to C: relation of C to the capturing clock decides
            std::string inFull = stripBit(m->nameOf(inNode));
            const sdc::InputDelay* decl = nullptr;
            for (auto& idl : cfg->constraints.inputDelays)
                for (auto& pr : idl.ports) if (nameMatches(pr.kind, pr.pattern, inNode, inFull)) decl = &idl;
            Relation inRel = decl ? rel_->relationByNames(decl->clock, domainClockName(dom)) : Relation::Unknown;
            bool declaredSync = decl && inRel == Relation::Same;
            std::string contract = decl ? "set_input_delay -clock " + decl->clock + " (" + decl->loc.file + ":" + std::to_string(decl->loc.line) + ") relates it to " +
                                              (declaredSync ? "this clock: declared synchronous" : "a clock that is " + std::string(relName(inRel)) + " to this one")
                                        : "no input-timing/CDC contract (set_input_delay) relates it to that clock";
            Severity sev = declaredSync ? Severity::Info : allSynced ? Severity::Info : Severity::Warning;
            Diagnostic d = mk("CDC-029", sev,
                              "top-level input '" + inFull + "' is captured in clock domain '" + m->domains[dom].name + "' by " +
                              std::to_string(dsts.size()) + " register bit(s)" +
                              (allSynced ? " through a recognised synchroniser" : " without a recognised synchroniser") + "; " + contract,
                              procOf(f0).loc, procOf(f0).instancePath, declaredSync || allSynced ? Confidence::Heuristic : Confidence::Unresolved);
            // What a waiver of this input accepted: changing the capture clock, edge or the declared contract must stale it.
            const std::string inputIdentity = stripBit(m->nameOf(inNode)) + ":" + endpoint(f0, inFull, dom).clockDomain + ":" +
                endpoint(f0, inFull, dom).edge + ":" + (decl ? decl->clock : "") + ":" + relName(inRel);
            Crossing inputCrossing;
            inputCrossing.launch.signal = inFull;
            if (decl) inputCrossing.launch.sdcClock = decl->clock;
            inputCrossing.capture = endpoint(f0, stripBit(flopName(f0)), dom);
            inputCrossing.relationship = relationshipName(inRel);
            inputCrossing.widthBits = dsts.size();
            inputCrossing.logicOnPath = inputLogic_.count({inNode, dom}) != 0;
            d.identityKey = inputIdentity;
            d.crossing = inputCrossing;
            d.signals = {inFull};
            d.evidence.push_back({"capture_register", flopName(f0), procOf(f0).loc});
            d.nextStep = declaredSync ? "Review the declared input timing contract against the external source; the declaration is trusted, not independently proved."
                         : allSynced ? "Review source stability and pulse duration and the recognised synchroniser's timing assumptions."
                         : inRel == Relation::Unknown ? "Declare the actual input clock intent (set_input_delay -clock) and review whether asynchronous-input synchronisation is needed."
                         : "Review a suitable receiving-domain input synchroniser or justify a waiver; a clock declaration does not repair this crossing.";
            // the synchroniser structure checks apply to primary inputs exactly as to register crossings
            if (!declaredSync) {
                int fan = 0, link = 0, mid = 0;
                for (auto di : dsts) if (chains[di].depth >= 2 || chains[di].linkLogic) { fan += chains[di].extraFanout; link += chains[di].linkLogic; mid += chains[di].midFanout; }
                auto sx = [&](const char* rule, std::string msg, Confidence cf) {
                    Diagnostic x = mk(rule, Severity::Warning, std::move(msg), procOf(f0).loc, procOf(f0).instancePath, cf);
                    x.identityKey = inputIdentity;
                    x.crossing = inputCrossing;
                    x.signals = {inFull};
                    evaluated[rule]++;
                    add(std::move(x));
                };
                if (fan) sx("CDC-007", "first synchroniser stage of input '" + inFull + "' also feeds logic other than the next stage", Confidence::Established);
                if (mid) sx("CDC-007", "an intermediate synchroniser stage of input '" + inFull + "' also feeds logic other than the next stage", Confidence::Established);
                if (link) sx("CDC-006", "combinational logic between the synchroniser stages of input '" + inFull + "'", Confidence::Established);
                if (inputLogic_.count({inNode, dom})) sx("CDC-008", "combinational logic between primary input '" + inFull + "' and its capturing register: glitches on the asynchronous input are sampled", Confidence::Established);
                bool merged = false;
                for (auto di : dsts) merged = merged || multiInputDst_.count(di);
                if (merged) sx("CDC-010", "primary input '" + inFull + "' is combined with other asynchronous inputs before it is sampled", Confidence::Heuristic);
                // other bits of the same input vector captured separately
                std::set<NodeId> bits;
                for (auto& [k2, d2] : inputsToDomain) if (k2.second == dom && stripBit(m->nameOf(k2.first)) == inFull) bits.insert(k2.first);
                if (bits.size() >= 2 && inNode == *bits.begin())
                    sx("CDC-009", std::to_string(bits.size()) + " bits of the asynchronous input '" + inFull + "' are captured separately: bit skew can sample an inconsistent value", Confidence::Established);
            }
            if (allSynced && (chains[dsts[0]].edgeDetector || chains[dsts[0]].depth >= 4)) {   // edge detector or a deserialising shift chain
                evaluated["CDC-044"]++;
                Diagnostic st = mk("CDC-044", Severity::Info, "synchronised input '" + inFull + "' feeds an edge detector or a shift chain: it is used as an asynchronous event/serial stream",
                                   procOf(f0).loc, procOf(f0).instancePath, Confidence::Heuristic);
                st.identityKey = inputIdentity;
                st.crossing = inputCrossing;
                st.signals = {inFull};
                st.proofState = ProofState::NotAttempted;
                st.evidence.push_back({"proof_obligation", "the stream's bit/pulse period must exceed two destination clock periods (oversampling); neither rate is constrained or checked", {}});
                add(std::move(st));
            }
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
                d.identityKey = sym + ":" + (&caps == &bbCaptures ? "blackbox" : &caps == &undrivenCaptures ? "undriven" : "truncated");
                d.evidence.push_back({"capture_register", sym, procOf(f0).loc});
                if (&caps == &undrivenCaptures)
                    for (const auto& leaf : undrivenLeaves[sym]) d.evidence.push_back({"undriven_source", leaf, {}});
                d.nextStep = "Model the source (provide the module, remove the unsupported construct) or review manually.";
                add(std::move(d));
                report->markIncomplete(gapScope, sym + ": " + what);
            }
        };
        unresolvedSource("CDC-023", bbCaptures, "may originate from an unmodeled black box", "blackboxes");
        unresolvedSource("CST-010", unresolvedCaptures, "depends on logic whose dependencies were truncated by a precision fallback", "graph-precision");
        unresolvedSource("CST-010", undrivenCaptures, "has an undriven or unmodeled source", "graph-precision");

        for (auto& [key, captures] : callCaptures) {
            const auto& [reg, issueId] = key;
            const auto& issue = m->callUncertainties[issueId];
            const auto& first = m->flops[captures.front()];
            Diagnostic diagnostic = mk("CST-010", Severity::Warning,
                "register '" + reg + "' depends on unresolved call '" + issue.call + "': " + issue.reason +
                "; its crossing dependencies and capture semantics are not fully established",
                procOf(first).loc, procOf(first).instancePath, Confidence::Unresolved);
            diagnostic.signals = {reg};
            diagnostic.proofState = ProofState::NotAttempted;
            diagnostic.evidence.push_back({"call", issue.call, issue.location});
            diagnostic.evidence.push_back({"capture_register", reg, procOf(first).loc});
            diagnostic.evidence.push_back({"proof_obligation", "establish the reads, writes and capture behavior of '" +
                issue.call + "' on the path to '" + reg + "': " + issue.reason, issue.location});
            diagnostic.nextStep = "Provide a supported subroutine body or review this capture path manually.";
            add(std::move(diagnostic));
            report->markIncomplete("graph-precision", reg + ": unresolved call '" + issue.call + "'");
        }

        analyseStructure();

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
} // namespace cdc
} // namespace sverity
