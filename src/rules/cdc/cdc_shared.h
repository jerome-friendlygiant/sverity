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
#include <sstream>
#include <limits>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <tuple>

#include "sverity/catalog.h"
#include "domains/cone.h"
#include "domains/batch_reach.h"
#include "domains/model.h"
#include "domains/relations.h"
#include "domains/reset_contracts_bind.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/ast/types/Type.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "rules/rules.h"
#include "rules/rdc/rdc_shared.h"
#include "sverity/waiver.h"   // globMatch

namespace sverity {
namespace cdc {

using namespace model;

bool hasExactInputSelfHold(const Model& model, NodeId target);
bool hasExactStageComparison(const Model& model, NodeId target, NodeId stage, NodeId next, char* idleValue = nullptr);
bool hasExactStageXor(const Model& model, NodeId target, NodeId stage, NodeId next, char* idleValue = nullptr);
bool hasExactStageInvertedAnd(const Model& model, NodeId target, NodeId stage, NodeId next, char* idleValue = nullptr);
bool hasExactCombinationalComparison(const Model& model, const Edge& stageEdge, const Edge& nextEdge,
                                    NodeId target, NodeId stage, NodeId next, char* idleValue = nullptr);
bool hasExactCombinationalXor(const Model& model, const Edge& stageEdge, const Edge& nextEdge,
                              NodeId target, NodeId stage, NodeId next, char* idleValue = nullptr);
bool hasExactCombinationalInvertedAnd(const Model& model, const Edge& stageEdge, const Edge& nextEdge,
                                      NodeId target, NodeId stage, NodeId next, char* idleValue = nullptr);

struct Chain {
    int depth = 1;
    bool callEffectsUnknown = false; // opaque call may alter this stage's D value
    bool linkLogic = false;      // logic between stage 1 and stage 2
    bool multiInputLinkLogic = false; // link logic matched via multi-input/combinational-wire stage-2 path
    NodeId linkTarget = kNoNode; // next stage fed through inter-stage logic
    bool extraFanout = false;    // stage-1 output also feeds something other than stage 2
    bool edgeDetector = false;   // some stage is combined by logic with its delayed copy (toggle receiver)
    struct DetectorSite { size_t earlierStage; int depth; NodeId target; char idleValue = '0'; };
    std::vector<DetectorSite> detectorSites;
    bool adjacentStageUse = false; // adjacent synchronizer stages jointly reach one data/control consumer
    std::vector<std::tuple<size_t, size_t, NodeId>> adjacentStageUses;
    size_t adjacentStageUseCount = 0; // total consumers; details are capped for bounded disclosure
    bool adjacentStageSearchIncomplete = false;
    bool adjacentStageSearchDone = false;
    size_t edgeDetectorBoundary = 0; // number of chain stages before the detector's delayed-copy register
    bool midFanout = false;      // an intermediate stage (2..n-1) also feeds something other than the next stage
    bool leaksDomain = false;    // a non-final stage output is captured by a register in another clock domain
    bool dualEdgeStage = false; // a required stage has unresolved dual-edge behavior
    bool clockMismatch = false;  // a stage is clocked on a different edge than the previous one
    bool resetMismatch = false;  // a stage has different asynchronous resets than the previous one
    bool feedbackWithFanout = false; // feedback already reported as first-stage fanout
    struct FanoutTarget { std::string name; SourceLocation loc; };
    std::vector<FanoutTarget> fanoutTargets; // evidence only; tap names are not waiver handles
    std::vector<uint32_t> stages;   // flop indices, stage 1 first
};

struct Finding {
    Diagnostic d;
};

using model::Condensation;
using model::condense;
using model::closeMasks;

class Shared {
public:
    Model* m = nullptr;     // owned by the Design (shared with other rule families)
    bool built = false;
    bool domainRulesEnabled_ = false;   // some CDC or RDC rule runs in this configuration
    bool crossingStages_ = false;       // some enabled rule needs the clock, reset, crossing or output stages
    std::map<std::string, std::vector<Diagnostic>> findings;
    std::map<std::string, uint64_t> evaluated;
    std::unique_ptr<ClockRelations> rel_;
    std::optional<model::ContractBindingResult> contracts_;   // reset_contracts binding, computed on first use (see contractBinding)
    Config const* cfg = nullptr;
    Report* report = nullptr;
    Design* design_ = nullptr;

    // ---- protocol / structure recognition, defined in cdc_protocol.cpp
    /// An output of a recognised synchroniser: `depth` stages reached at this register, `full` when its chain reaches the
    /// required depth, `head` the chain's first stage (one synchroniser has one head, whichever of its stages is read).
    struct SyncOut {
        uint32_t srcDomain = 0, dstDomain = 0; std::string hier, srcName; NodeId srcQ = kNoNode; uint64_t width = 0;
        int depth = 0; bool full = false; uint32_t head = 0;
        std::string sourceClock; // primary-input launch clock, when stated by set_input_delay
        bool sourceIncomplete = false;
        bool stageMismatch = false;
        bool dualEdgeStage = false;
    };
    std::unordered_set<const slang::ast::ValueSymbol*> grayNets_;   // nets assigned b ^ (b >> 1) (binary-to-Gray)
    std::unordered_set<const slang::ast::ValueSymbol*> opaqueFunctionNets_;
    std::map<NodeId, SyncOut> syncedOut_;                           // Q of the output stage of a recognised synchroniser and its followers
    std::map<NodeId, SyncOut> earlyQualifierTap_;                   // Stage after the first, but before the required output depth
    void findGrayNets();
    bool isGrayFlop(NodeId q) const;
    bool hasOpaqueFunction(NodeId q) const;
    bool searchOpaqueFunction(NodeId q) const;
    mutable std::vector<int8_t> opaqueFunctionOf_;   // per node: -1 not asked yet; the answer depends only on the node
    NodeId aliasRoot(NodeId n) const { return m->aliasRoot(n); }
    /// True when `d` is enabled/selected by a control that is the output of a recognised synchroniser whose source
    /// is in `srcDomain` (the qualifier pattern: payload captured under a synchronised valid).
    enum class Qualification { ProvenNecessary, Disproven, Undecided };
    struct QualifierResult {
        Qualification state;
        std::string reason;
        std::set<std::pair<NodeId, bool>> necessaryQualifiers;
    };
    // Classes for the qualification cone views: the launch domain of a register, or listed (unidentified clock, several blocks).
    mutable std::optional<SourceClasses> qualifierClasses_;
    // A capture register's guarded writes with combinational qualifier drivers expanded (ctrlFromSynced). They depend only on the
    // register, and one register is asked about once per crossing group it captures, so they are kept.
    struct ExpandedWrite {
        std::shared_ptr<const std::vector<GuardCondition>> guards;   // the model's list for a write as written; a new one once expanded
        std::shared_ptr<const std::vector<NodeId>> dataDependencies;
        std::shared_ptr<const std::vector<NodeId>> controlDependencies;
        bool dataApproximate = false;
        bool controlApproximate = false;
        bool uncertainCall = false;
        bool incompleteComb = false;
        bool expansionLimited = false;
        unsigned depth = 0;
        std::unordered_set<NodeId> visitedComb;
    };
    struct ExpandedWrites { std::vector<ExpandedWrite> writes; bool limited = false; };
    mutable std::unordered_map<NodeId, ExpandedWrites> expandedWritesOf_;
    // The registers and inputs in a capture's cone that are alternate data sources, and those on the launch clock, per
    // (capture, launch domain, launch clock): sorted. Independent of the group's own source bits, which are tested first.
    struct QualifierSources { std::shared_ptr<const std::vector<NodeId>> alternate, sameSource; };
    // The source sets of different captures are often equal (a bus behind one multiplexer): each distinct set is stored once.
    mutable std::unordered_map<uint64_t, std::vector<std::shared_ptr<const std::vector<NodeId>>>> internedSourceSets_;
    std::shared_ptr<const std::vector<NodeId>> internSourceSet(std::vector<NodeId> nodes) const {
        uint64_t hash = nodes.size();
        for (const NodeId node : nodes) hash = hash * 1099511628211u ^ node;
        auto& bucket = internedSourceSets_[hash];
        for (const auto& known : bucket) if (*known == nodes) return known;
        nodes.shrink_to_fit();
        qualifierSourcesBytes_ += nodes.capacity() * sizeof(NodeId) + 64;
        bucket.push_back(std::make_shared<const std::vector<NodeId>>(std::move(nodes)));
        return bucket.back();
    }
    mutable std::map<std::tuple<NodeId, uint32_t, std::string>, QualifierSources> qualifierSourcesOf_;
    // Per (capture, launch domain), with no source clock: the sets computed ahead for the group stage, then released.
    mutable std::map<std::pair<NodeId, uint32_t>, QualifierSources> precomputedSources_;
    void precomputeQualifierSources(std::vector<std::pair<uint32_t, uint32_t>> keys) const;
    bool qualifierLocalFlop(const Flop& launch, uint32_t captureDomain) const;
    bool qualifierLocalInput(NodeId inNode, uint32_t captureDomain) const;
    bool qualifierSameSourceInput(NodeId inNode, uint32_t srcDomain, const std::string& sourceClock) const;
    bool qualifierSameSourceFlop(const Flop& launch, uint32_t srcDomain, const std::string& sourceClock) const;
    // Per (capture, source domain, source clock): the answer of a probe (ctrlFromSynced with `probed`) and the nodes it tested.
    using QualifierKey = std::tuple<NodeId, uint32_t, std::string>;
    struct QualifierProbe { QualifierResult result; std::vector<NodeId> tested; };
    mutable std::map<QualifierKey, QualifierProbe> probes_;
    mutable size_t probeBytes_ = 0;
    mutable std::vector<uint32_t> probeMark_;   // nodes a probe has listed, by generation
    mutable uint32_t probeGen_ = 0;
    bool probeQualifiers_ = false;   // set while reconvergence qualifies single source bits
    // Both caches are bounded: past the budget they are emptied and refilled (slower, same answers).
    static constexpr size_t kQualifierCacheBytes = size_t{1} << 27;
    // Time spent in ctrlFromSynced by part (expansion, source sets, write checks) and in pathWitness, for --timings.
    mutable uint64_t qualifierNanos_[3] = {0, 0, 0}, qualifierCalls_ = 0, witnessNanos_ = 0, qualifierExpansions_ = 0, qualifierSourceSets_ = 0, probeHits_ = 0, qualifierExpansionClears_ = 0, qualifierSourceClears_ = 0;
    mutable uint64_t witnessCalls_ = 0, witnessPositions_ = 0, witnessExpanded_ = 0, witnessFound_ = 0, witnessEdgeScans_ = 0;
    // Per-node mark of the source bits of one ctrlFromSynced call (both roles), valid while equal to the call's mark; the call also
    // stamps its search memo with it.
    mutable std::vector<uint32_t> controlSourceMark_;
    mutable uint32_t sourceMarkGen_ = 0;
    // The interned source sets marked last (kept alive, so a new set never reuses the address), with the generation they carry:
    // a capture asked about once per raw source bit marks its sets once.
    mutable std::shared_ptr<const std::vector<NodeId>> markedControlSet_, markedDataSet_;
    mutable std::vector<uint32_t> controlSetMark_, dataSetMark_;
    mutable uint32_t controlSetGen_ = 0, dataSetGen_ = 0;
    // Per-call memo of the source searches of ctrlFromSynced, by start node and role (0 data, 1 control): valid while the stamp
    // equals the call's mark; the value holds precise (1), approximate (2) and limited (4). And the search's reused work list.
    mutable std::vector<uint32_t> pathMemoStamp_[2];
    mutable std::vector<uint8_t> pathMemoValue_[2];
    mutable std::vector<std::pair<NodeId, bool>> sourceSearchPending_;
    // Source-set-independent trace of the bounded source search of ctrlFromSynced, by (start, role): see searchSourcePaths.
    struct SourceTraceState { NodeId node; bool approximate; bool leaf; };
    struct SourceTrace {
        std::vector<std::pair<NodeId, bool>> leaves;   // leaves among the first 256 new states, in pop order (node, approximate)
        std::vector<SourceTraceState> post;            // new states past the 256th, in pop order, up to the first non-leaf
        bool overflow = false;                         // the post run exceeded its bound, or a possible source has a combinational
                                                       // driver: the call searches live
    };
    mutable std::unordered_map<uint64_t, SourceTrace> sourceTraces_;
    mutable size_t sourceTraceBytes_ = 0;
    static constexpr size_t kSourceTraceBytes = size_t{1} << 27;
    mutable size_t expandedWritesBytes_ = 0, qualifierSourcesBytes_ = 0;
    mutable std::map<std::tuple<uint32_t, uint32_t, std::string>, uint64_t> qualifierKeys_;
    // With `probed`, the source set is taken to hold none of the nodes the call tests, and every node tested is appended to
    // it: the source bits matter only through those tests, so a set of bits none of which is listed gets the same answer.
    QualifierResult ctrlFromSynced(const Flop& d, uint32_t srcDomain, const std::string& sourceClock = {},
                                   const std::set<NodeId>& sourceBits = {}, std::vector<NodeId>* probed = nullptr) const;
    bool hasCtrlEdge(const Flop& d) const;
    struct InputPath { bool data = false, control = false, uncertain = false; };
    bool counterLike(const std::set<NodeId>& srcBits);
    bool hasLiveBinaryCounterUpdate(const std::set<NodeId>& srcBits);
    bool grayUnitStep(const std::set<NodeId>& grayBits);
    /// SDC path exceptions of `kind` whose from/to refs match the two nodes (at least one side must be named).
    std::vector<const sdc::PathException*> exceptionsFor(NodeId src, NodeId dst, bool anyKind,
                                                         sdc::PathException::Kind kind) const;

    /// Marks a scope incomplete. The clock, reset and constraint scopes describe what a CDC or RDC rule needs; a run with
    /// none of them enabled (a lint goal) loses nothing by an absent constraint file, so those scopes are left alone.
    void incomplete(const std::string& scope, const std::string& reason) {
        static const std::set<std::string, std::less<>> domainScopes = {"clock-extraction", "reset-extraction", "clock-relationships",
            "constraints", "case-analysis-modes", "payload-qualification", "cdc-017", "cdc-015-handshake-reachability"};
        if (!domainRulesEnabled_ && domainScopes.count(scope)) return;
        report->markIncomplete(scope, reason);
    }

    void ensure(RuleContext& ctx) {
        if (built) return;
        built = true;
        cfg = &ctx.config;
        report = &ctx.report;
        for (const auto& rule : Catalog::embedded().rules())
            if ((rule.category == "CDC" || rule.category == "RDC") && !ctx.config.disabledRules.count(rule.id)) domainRulesEnabled_ = true;
        for (const char* sc : {"clock-extraction", "reset-extraction", "clock-relationships", "constraints",
                               "graph-precision", "blackboxes"})
            report->declareScope(sc);
        design_ = &ctx.design;
        m = &getModel(ctx.design);
        // Only the crossing, clock, reset and output stages need the shared cones (the memory of a large design). A run whose enabled
        // rules are all among the constraint, black-box and precision-fallback rules needs none of them: see ANALYSIS.md, "CDC pass stages".
        crossingStages_ = domainRulesEnabled_;
        for (const char* id : {"CST-001", "CST-004", "CST-011", "CST-012"})
            if (!ctx.config.disabledRules.count(id)) crossingStages_ = true;
        cones_ = &model::sharedCones(*m);
        auto timed = [&](const char* phase, auto&& stage) {
            const auto start = Timings::Clock::now();
            stage();
            if (ctx.config.timings) ctx.config.timings->record(phase, start);
        };
        if (crossingStages_) timed("cdc:gray_nets", [&] { findGrayNets(); });
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
        timed("cdc:constraints", [&] {
            mapConstraints();
            analyseFallbacks();
            analyseBlackBoxes();
            analyseConstraints();
        });
        if (crossingStages_) {
            timed("cdc:clocks", [&] { analyseClocks(); });
            timed("cdc:resets", [&] { analyseResets(); });
            timed("cdc:crossings", [&] { analyseCrossings(); });
            timed("cdc:outputs", [&] { analyseOutputs(); });
        } else {
            timed("cdc:capture_uncertainty", [&] { analyseCaptureUncertainty(); });
        }
        if (ctx.config.timings) ctx.config.timings->checkpoint(cones_->telemetry());
        releaseAnalysisCaches();
    }

    // Each rule's findings are handed to the report once; moving them leaves no second copy of a SoC's diagnostics.
    void flush(const std::string& rule, RuleContext& ctx) {
        if (auto it = findings.find(rule); it != findings.end()) {
            for (auto& d : it->second) ctx.report.add(std::move(d));
            findings.erase(it);
        }
        ctx.objectsEvaluated = evaluated[rule];
    }

private:
    // The findings are complete: what only sped up the analysis is released before the report holds them all.
    void releaseAnalysisCaches() {
        const auto drop = [](auto& container) { container = std::decay_t<decltype(container)>(); };
        drop(opaqueFunctionOf_); qualifierClasses_.reset(); drop(expandedWritesOf_); drop(internedSourceSets_);
        drop(qualifierSourcesOf_); drop(precomputedSources_); drop(probes_); feedbackGraph_.reset(); probeBytes_ = 0; drop(probeMark_); probeGen_ = 0; drop(controlSourceMark_); markedControlSet_.reset(); markedDataSet_.reset();
        drop(controlSetMark_); drop(dataSetMark_); drop(pathMemoStamp_[0]); drop(pathMemoStamp_[1]);
        drop(pathMemoValue_[0]); drop(pathMemoValue_[1]); drop(sourceSearchPending_); drop(sourceTraces_); drop(qualifierKeys_);
        drop(backStamp_); drop(foreStamp_); drop(fanoutStamp_); drop(feedbackOf_); drop(proceduralFeedbackOf_);
        drop(adjacentConsumerFor_); adjacentConsumerIndexReady_ = false;
        drop(witnessStamp_); drop(witnessEdge_); drop(witnessSource_); drop(witnessMasks_); drop(witnessInStart_); drop(witnessInFrom_); drop(witnessInEdge_);
        expandedWritesBytes_ = qualifierSourcesBytes_ = sourceTraceBytes_ = 0;
        controlSetGen_ = dataSetGen_ = sourceMarkGen_ = feedbackGen_ = 0;
        witnessGen_ = witnessSourceGen_ = 0;
        cones_->releaseCaches();
    }
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
    void add(Diagnostic d) {
        // Disabled findings are never flushed; keep the analysis and scope effects,
        // but do not retain diagnostic payloads that this run cannot report.
        if (cfg && cfg->disabledRules.count(d.ruleId)) return;
        findings[d.ruleId].push_back(std::move(d));
    }

    const ProcInfo& procOf(const Flop& f) const { return m->procs[f.proc]; }
    std::string flopName(const Flop& f) const { return m->nameOf(f.q); }

    // Compare asynchronous resets by the external net and effective assertion
    // polarity. Follow only exact wire/inverter aliases; logic remains distinct.
    std::set<std::pair<NodeId, bool>> asyncResetSignature(const Flop& f) const {
        std::set<std::pair<NodeId, bool>> result;
        for (const auto& reset : f.resets) {
            NodeId node = reset.signal;
            bool activeLow = reset.activeLow;
            bool exact = node != kNoNode;
            std::set<NodeId> visited;
            bool reachedRoot = false;
            for (unsigned step = 0; exact && step < 64; ++step) {
                if (!visited.insert(node).second) { exact = false; break; }
                const auto& incoming = m->in[node];
                if (incoming.empty()) { reachedRoot = true; break; }
                if (incoming.size() != 1 || m->constDrivers.contains(node) ||
                    (node < m->blackBoxDriven.size() && m->blackBoxDriven[node]) ||
                    (node < m->unresolvedDriven.size() && m->unresolvedDriven[node])) { exact = false; break; }
                const auto& edge = m->edges[incoming[0]];
                if (edge.type != EdgeType::Comb || edge.ctrl || edge.approximate || edge.shared ||
                    !(edge.exact || edge.flip)) { exact = false; break; }
                activeLow = activeLow != edge.flip;
                node = edge.from;
            }
            if (!reachedRoot) exact = false;
            result.emplace(exact ? node : reset.root.node, activeLow);
        }
        return result;
    }

    // ------------------------------------------------------------ constraints <-> domains
    void mapConstraints() { rel_ = std::make_unique<ClockRelations>(*m, *cfg); }
    Relation relation(uint32_t a, uint32_t b) const { return rel_->relation(a, b); }
    const std::string& domainClockName(uint32_t d) const { return rel_->clockName(d); }
    bool bitMatches(const std::string& kind, const std::string& pattern, NodeId n) const { return rel_->bitMatches(kind, pattern, n, rel_->rtlBitName(n)); }
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
            incomplete("graph-precision", cat + ": " + std::to_string(v.size()) + " occurrence(s)");
        }
    }
    void analyseBlackBoxes() {
        evaluated["CST-005"] = m->blackBoxes.size();
        std::map<std::string, std::vector<const BlackBoxInst*>> byMod;
        for (auto& b : m->blackBoxes) byMod[b.module].push_back(&b);
        for (auto& [mod, v] : byMod) {
            Diagnostic d = mk("CST-005", Severity::Warning,
                              std::to_string(v.size()) + " instance(s) of " + (v[0]->attributed ? "module '" + mod + "', which is attributed as a black box" : "unknown module '" + mod + "'") +
                              " (black box, UNMODELED): its clock/reset/CDC behaviour is not analysed" +
                              std::string(v[0]->declared ? " (declared in a --library file: port directions and roles are known, behaviour is not)"
                                          : v[0]->attributed ? " (its body is not the hardware and is not walked)" : ""),
                              v[0]->loc, v[0]->hierarchy, Confidence::Unresolved);
            for (size_t i = 0; i < v.size() && i < 8; i++) {
                d.evidence.push_back({"blackbox_instance", v[i]->hierarchy, v[i]->loc});
                d.evidence.push_back({"site_signal", v[i]->hierarchy, v[i]->loc});
            }
            // List nets connected to output-capable ports and the registers/ports they drive directly so black-box
            // findings name the affected state.
            std::unordered_set<std::string> seenSig;
            auto pushSig = [&](const std::string& raw) {
                if (raw.empty() || raw == "<unknown>" || d.signals.size() >= 32) return;
                if (seenSig.insert(raw).second) d.signals.push_back(raw);
            };
            for (const BlackBoxInst* inst : v) {
                for (const auto& port : inst->ports) {
                    if (port.dir == PortDir::In) continue;
                    for (NodeId start : port.nodes) {
                        if (start == kNoNode || start >= m->nodes.size()) continue;
                        pushSig(stripBit(m->nameOf(start)));
                        std::vector<NodeId> work{start};
                        std::unordered_set<NodeId> seen{start};
                        for (size_t wi = 0; wi < work.size() && seen.size() < 64; wi++) {
                            const NodeId u = work[wi];
                            if (u != start && u < m->topOutput.size() && m->topOutput[u])
                                pushSig(stripBit(m->nameOf(u)));
                            if (u != start && m->flopOfQ.count(u)) continue;
                            if (u >= m->out.size()) continue;
                            for (auto ei : m->out[u]) {
                                const Edge& e = m->edges[ei];
                                if (e.type == EdgeType::Seq || e.type == EdgeType::AsyncCtl) {
                                    pushSig(stripBit(m->nameOf(e.to)));
                                } else if (e.type == EdgeType::Comb && seen.insert(e.to).second) {
                                    work.push_back(e.to);
                                }
                            }
                        }
                    }
                }
            }
            d.nextStep = "Provide the module source or a model with clock/reset/CDC metadata.";
            add(std::move(d));
            incomplete("blackboxes", "unmodeled black box '" + mod + "'");
        }
    }

    bool inputDelayMatches(const sdc::ObjectRef& ref, NodeId node) const {
        return rel_->inputDelayMatches(ref, node);
    }

    // ------------------------------------------------------------ CST-009 / CST-018 / CST-011 / CST-002 / CST-003
    void analyseConstraints() {
        auto& c = cfg->constraints;
        evaluated["CST-009"] = c.unsupported.size();
        evaluated["CST-018"] = c.problems.size();
        auto extractSdcSignals = [](const std::string& cmd, const std::vector<std::string>& texts) {
            std::vector<std::string> signals;
            auto push = [&](std::string tok) {
                if (tok.empty() || signals.size() >= 16) return;
                if (std::find(signals.begin(), signals.end(), tok) == signals.end())
                    signals.push_back(std::move(tok));
            };
            push(cmd);
            static const char* const getters[] = {"get_clocks", "get_ports", "get_pins", "get_nets", "get_cells"};
            auto isWs = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
            auto hasMeta = [](std::string_view tok) {
                if (tok.empty() || tok.front() == '-') return true;
                for (char ch : tok) {
                    if (ch == '*' || ch == '?' || ch == '$' || ch == '[' || ch == ']' ||
                        ch == '(' || ch == ')' || ch == '{' || ch == '}' || ch == '=' ||
                        ch == '!' || ch == '<' || ch == '>' || ch == '~' || ch == '|' ||
                        ch == '^' || ch == '+' || ch == ';' || ch == ',' || ch == '\\')
                        return true;
                }
                return false;
            };
            struct TclWord { std::string text; bool quoted = false; };
            auto parseWords = [&](std::string_view s) {
                std::vector<TclWord> words;
                size_t i = 0;
                while (i < s.size()) {
                    while (i < s.size() && isWs(s[i])) ++i;
                    if (i >= s.size()) break;
                    if (s[i] == '{') {
                        size_t depth = 1, start = ++i;
                        while (i < s.size() && depth > 0) {
                            if (s[i] == '\\' && i + 1 < s.size()) { i += 2; continue; }
                            if (s[i] == '{') ++depth;
                            else if (s[i] == '}') --depth;
                            if (depth > 0) ++i;
                        }
                        words.push_back({std::string(s.substr(start, i - start)), true});
                        if (i < s.size() && s[i] == '}') ++i;
                    } else if (s[i] == '"') {
                        std::string val;
                        ++i;
                        while (i < s.size() && s[i] != '"') {
                            if (s[i] == '\\' && i + 1 < s.size()) ++i;
                            if (i < s.size()) val.push_back(s[i++]);
                        }
                        if (i < s.size() && s[i] == '"') ++i;
                        words.push_back({std::move(val), true});
                    } else {
                        std::string val;
                        while (i < s.size() && !isWs(s[i])) {
                            if (s[i] == '\\' && i + 1 < s.size()) ++i;
                            if (i < s.size()) val.push_back(s[i++]);
                        }
                        words.push_back({std::move(val), false});
                    }
                }
                return words;
            };
            for (const std::string& text : texts) {
                size_t outerBrace = 0;
                for (size_t pos = 0; pos < text.size() && signals.size() < 16; ++pos) {
                    if (text[pos] == '\\' && pos + 1 < text.size()) { ++pos; continue; }
                    if (text[pos] == '{') { ++outerBrace; continue; }
                    if (text[pos] == '}' && outerBrace > 0) { --outerBrace; continue; }
                    if (text[pos] != '[' || outerBrace > 0) continue;
                    size_t depth = 1, end = pos + 1, braceDepth = 0;
                    while (end < text.size() && depth > 0) {
                        if (text[end] == '\\' && end + 1 < text.size()) { end += 2; continue; }
                        if (text[end] == '{') ++braceDepth;
                        else if (text[end] == '}' && braceDepth > 0) --braceDepth;
                        else if (braceDepth == 0) {
                            if (text[end] == '[') ++depth;
                            else if (text[end] == ']') --depth;
                        }
                        ++end;
                    }
                    if (depth != 0) continue;
                    auto words = parseWords(std::string_view(text.data() + pos + 1, end - pos - 2));
                    if (words.empty() || words[0].quoted) continue;
                    const std::string& head = words[0].text;
                    if (std::none_of(std::begin(getters), std::end(getters), [&](const char* g) { return head == g; }))
                        continue;
                    bool isRegexp = false;
                    for (size_t wi = 1; wi < words.size(); ++wi) {
                        if (words[wi].quoted) continue;
                        if (words[wi].text == "-filter" || words[wi].text == "-of_objects" || words[wi].text == "-of") {
                            if (wi + 1 < words.size()) ++wi;
                            continue;
                        }
                        if (words[wi].text == "-regexp") { isRegexp = true; break; }
                    }
                    if (isRegexp) continue;
                    for (size_t wi = 1; wi < words.size() && signals.size() < 16; ++wi) {
                        const auto& w = words[wi];
                        if (!w.quoted && !w.text.empty() && w.text[0] == '-') {
                            if ((w.text == "-filter" || w.text == "-of_objects" || w.text == "-of") && wi + 1 < words.size())
                                ++wi;
                            continue;
                        }
                        if (w.quoted) {
                            size_t ti = 0;
                            while (ti < w.text.size() && signals.size() < 16) {
                                while (ti < w.text.size() && isWs(w.text[ti])) ++ti;
                                size_t ts = ti;
                                while (ti < w.text.size() && !isWs(w.text[ti])) ++ti;
                                std::string_view sub(w.text.data() + ts, ti - ts);
                                if (!hasMeta(sub)) push(std::string(sub));
                            }
                        } else if (!hasMeta(w.text)) {
                            push(w.text);
                        }
                    }
                }
            }
            return signals;
        };
        std::map<std::string, std::vector<const sdc::Unsupported*>> byCmd;
        for (auto& u : c.unsupported) byCmd[u.command].push_back(&u);
        for (auto& [cmd, v] : byCmd) {
            Diagnostic d = mk("CST-009", Severity::Warning,
                              "SDC command '" + cmd + "' is not interpreted by SVerity (" + std::to_string(v.size()) +
                              " occurrence(s)); the constraint it expresses is NOT applied",
                              v[0]->loc, {}, Confidence::Unresolved);
            std::vector<std::string> texts;
            texts.reserve(v.size());
            for (const auto* u : v) texts.push_back(u->text);
            d.signals = extractSdcSignals(cmd, texts);
            d.evidence.push_back({"sdc_command", v[0]->text, v[0]->loc});
            d.nextStep = "Check whether the missing constraint changes any CDC conclusion; supported commands are listed in docs.";
            add(std::move(d));
            incomplete("constraints", "unsupported SDC command '" + cmd + "'");
        }
        for (auto& p : c.problems) {
            add(mk("CST-018", Severity::Warning, "constraint parse problem: " + p.message, p.loc, {}, Confidence::Unresolved));
            incomplete("constraints", "constraint parse problem: " + p.message);
        }
        // Validate every reference before applying an output contract.
        auto problem = [&](const std::string& message, const SourceLocation& loc) {
            evaluated["CST-018"]++;
            add(mk("CST-018", Severity::Warning, message, loc, {}, Confidence::Unresolved));
            incomplete("constraints", message);
        };
        auto resolve = [&](const std::vector<sdc::ObjectRef>& refs, bool output, const SourceLocation& loc, bool bits = false) {
            bool valid = true;
            for (const auto& ref : refs) {
                bool hit = false;
                for (NodeId n = 0; n < m->nodes.size() && !hit; ++n) {
                    if (output && !m->topOutput[n]) continue;
                    const auto kind = ref.kind.empty() ? "get_ports" : ref.kind;
                    if (!rel_->mayNameMatch(kind, ref.pattern, n)) continue;
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
            d.signals = extractSdcSignals(command, {text});
            d.evidence.push_back({"sdc_command", text, loc});
            d.nextStep = "Review this assumption manually; the declaration does not establish safety.";
            add(std::move(d));
            incomplete("constraints", std::string("unapplied SDC command '") + command + "'");
        };
        for (const auto& d : c.outputDelays) {
            bool valid = resolve(d.ports, true, d.loc, true);
            bool clock = false;
            for (const auto& ck : c.clocks)
                clock |= (ck.name.empty() && !ck.sources.empty() ? ck.sources[0].pattern : ck.name) == d.clock;
            if (!clock) problem("set_output_delay: clock '" + d.clock + "' resolves to no declared clock", d.loc);
            if (valid && clock) outputContracts_.push_back(&d);
        }
        if (!c.inputDelays.empty()) {
            std::vector<NodeId> inputs;
            for (NodeId n = 0; n < m->topInput.size(); ++n)
                if (m->topInput[n]) inputs.push_back(n);
            for (const auto& d : c.inputDelays) {
                for (const auto& ref : d.ports) {
                    const bool hit = std::any_of(inputs.begin(), inputs.end(), [&](NodeId n) {
                        return inputDelayMatches(ref, n);
                    });
                    if (!hit) {
                        problem("set_input_delay: object '" + ref.pattern +
                                "' resolves to no supported top-level input port; entire declaration is NOT applied", d.loc);
                    }
                }
                const bool clock = std::any_of(c.clocks.begin(), c.clocks.end(), [&](const auto& ck) {
                    return (ck.name.empty() && !ck.sources.empty() ? ck.sources[0].pattern : ck.name) == d.clock;
                });
                if (!clock) problem("set_input_delay: clock '" + d.clock + "' resolves to no declared clock", d.loc);
            }
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
            incomplete("constraints", "selected case-analysis mode does not cover alternatives");
            incomplete("case-analysis-modes", "alternative case-analysis values are not analysed");
        }
        if (!cfg->constraintsProvided) {
            incomplete("constraints", "no constraint file provided: clock relationships are unknown");
        }
        // Keep declaration history for conflicts, but use the latest definition for ancestry.
        evaluated["CST-003"] = c.clocks.size() + c.clockGroups.size();
        std::map<std::string, const sdc::ClockDef*> latest;
        for (const auto& ck : c.clocks) {
            auto it = latest.find(ck.name);
            if (it != latest.end() && !ck.add) {
                const auto& old = *it->second;
                if (old.periodNs != ck.periodNs || old.waveform != ck.waveform || old.sources != ck.sources ||
                    old.generated != ck.generated || old.sourceObjects != ck.sourceObjects ||
                    old.explicitMasterClock != ck.explicitMasterClock || old.divideBy != ck.divideBy ||
                    old.multiplyBy != ck.multiplyBy || old.waveformOptions != ck.waveformOptions) {
                    auto d = mk("CST-003", Severity::Warning, "clock '" + ck.name +
                                "' is redeclared with a different period, waveform or source without -add; "
                                "the later declaration overrides the earlier one and is used by analysis",
                                ck.loc, {}, Confidence::Established);
                    d.evidence.push_back({"earlier_declaration", "clock '" + old.name + "'", old.loc});
                    d.evidence.push_back({"later_declaration", "clock '" + ck.name + "'", ck.loc});
                    add(std::move(d));
                }
            }
            latest[ck.name] = &ck;
        }
        // Each object pattern is resolved to its nodes once: comparing every pair of clocks node by node does not scale.
        std::map<std::pair<std::string, std::string>, std::set<NodeId>> resolved;
        auto nodesOf = [&](const sdc::ObjectRef& ref) -> const std::set<NodeId>& {
            auto [it, fresh] = resolved.try_emplace({ref.kind, ref.pattern});
            if (fresh)
                for (NodeId n = 0; n < m->nodes.size(); ++n)
                    if (rel_->mayNameMatch(ref.kind, ref.pattern, n) &&
                        (nameMatches(ref.kind, ref.pattern, n, stripBit(m->nameOf(n))) || bitMatches(ref.kind, ref.pattern, n))) it->second.insert(n);
            return it->second;
        };
        // A source object can carry several clocks (-add); retain every possible parent.
        std::map<std::string, std::set<std::string>> parents;
        for (const auto& [name, ck] : latest) {
            if (!ck->generated) continue;
            if (!ck->explicitMasterClock.empty()) {
                // -master_clock takes a clock collection, so a pattern selects every declared clock it matches.
                for (const auto& [candidate, master] : latest)
                    if (candidate != name && globMatch(ck->explicitMasterClock, candidate)) parents[name].insert(candidate);
            } else {
                for (const auto& [candidate, master] : latest) {
                    if (candidate == name) continue;
                    // A pin connected to a port is the same signal: compare alias roots, so a master on the port reaches the pin.
                    bool overlaps = false;
                    for (const auto& src : ck->sourceObjects)
                        for (const auto& target : master->sources) {
                            std::set<NodeId> roots;
                            for (auto n : nodesOf(target)) roots.insert(aliasRoot(n));
                            for (auto n : nodesOf(src)) if (roots.count(aliasRoot(n))) { overlaps = true; break; }
                        }
                    if (overlaps) parents[name].insert(candidate);
                }
            }
        }
        for (const auto& g : c.clockGroups) {
            if (g.kind == sdc::ClockGroups::Kind::Unspecified) continue;
            auto groupOf = [&](const std::string& name) {
                for (size_t i = 0; i < g.groups.size(); ++i)
                    for (const auto& ref : g.groups[i])
                        if (globMatch(ref.pattern, name)) return int(i);
                return -1;
            };
            for (const auto& [name, ck] : latest) {
                if (!ck->generated) continue;
                std::set<std::string> visited{name};
                std::vector<std::string> pending(parents[name].begin(), parents[name].end());
                while (!pending.empty()) {
                    const auto ancestor = pending.back(); pending.pop_back();
                    if (!visited.insert(ancestor).second) continue;
                    pending.insert(pending.end(), parents[ancestor].begin(), parents[ancestor].end());
                    const int childGroup = groupOf(name), masterGroup = groupOf(ancestor);
                    if (childGroup < 0 || masterGroup < 0 || childGroup == masterGroup) continue;
                    const auto relation = rel_->relationByNames(name, ancestor);
                    auto d = mk("CST-003", Severity::Warning, "set_clock_groups separates generated clock '" + name +
                                "' from its declared ancestor '" + ancestor +
                                "': derivation says they are related, but this group cuts STA timing between them; "
                                "analysis uses " + relName(relation) +
                                " (first matching group declaration wins; generated derivation does not establish synchrony)",
                                g.loc, {}, Confidence::Heuristic);
                    d.evidence.push_back({"generated_declaration", "clock '" + name + "'", ck->loc});
                    d.evidence.push_back({"ancestor_declaration", "clock '" + ancestor + "'", latest.at(ancestor)->loc});
                    add(std::move(d));
                }
            }
        }
        for (const auto& ck : c.clocks) {
            if (!ck.generated) continue;
            auto unresolved = [&](const std::string& detail) {
                add(mk("CST-002", Severity::Warning, "generated clock '" + ck.name + "': " + detail +
                       "; derivation does not establish a relationship (UNKNOWN unless clock groups relate the names)",
                       ck.loc, {}, Confidence::Unresolved));
                incomplete("constraints", detail);
            };
            if (!ck.explicitMasterClock.empty() && std::none_of(latest.begin(), latest.end(), [&](const auto& entry) {
                    return entry.first != ck.name && globMatch(ck.explicitMasterClock, entry.first); }))
                unresolved("-master_clock '" + ck.explicitMasterClock + "' names no declared clock");
            for (const auto& src : ck.sourceObjects) {
                if (nodesOf(src).empty()) unresolved("-source '" + src.pattern + "' resolves to no design object");
            }
        }
        // CST-003: one RTL clock root carrying active SDC clocks declared asynchronous to each other
        for (uint32_t di = 0; di < m->domains.size(); ++di) {
            if (!rel_->hasMultipleClocks(di)) continue;
            const NodeId root = m->domains[di].root;
            const std::string full = stripBit(m->nameOf(root));
            std::vector<std::string> activeNames;
            for (const auto& [name, ck] : latest) {
                for (const auto& s : ck->sources) {
                    if (rel_->clockSourceMatches(s, root, full)) {
                        activeNames.push_back(name);
                        break;
                    }
                }
            }
            if (activeNames.size() < 2) continue;
            bool hasAsync = false;
            for (size_t i = 0; i < activeNames.size() && !hasAsync; ++i)
                for (size_t j = i + 1; j < activeNames.size() && !hasAsync; ++j)
                    if (rel_->relationByNames(activeNames[i], activeNames[j]) == Relation::Async) hasAsync = true;
            if (!hasAsync) continue;
            std::string joined;
            for (size_t i = 0; i < activeNames.size(); ++i) {
                if (i) joined += ", ";
                joined += "'" + activeNames[i] + "'";
            }
            evaluated["CST-003"]++;
            auto d = mk("CST-003", Severity::Warning,
                        "clock root '" + m->domains[di].name + "' carries multiple active SDC clocks (" + joined +
                        ") declared asynchronous by set_clock_groups: its relationship is unresolved",
                        latest.at(activeNames.front())->loc, {}, Confidence::Unresolved);
            d.nextStep = "Declare one active clock per RTL root per mode run, or analyse each clock mode separately.";
            add(std::move(d));
            incomplete("constraints", "clock root '" + m->domains[di].name + "' carries asynchronous SDC clock aliases");
            incomplete("clock-relationships", "clock root '" + m->domains[di].name + "' carries asynchronous SDC clock aliases");
        }
        // CST-002: clock source patterns and path-exception object selectors that match no design object
        evaluated["CST-002"] = c.clocks.size() + c.exceptions.size();
        if (!c.clocks.empty() || !c.exceptions.empty()) {
            const NodeId nodeCount = static_cast<NodeId>(m->nodes.size());
            auto refMatchesAnyNode = [&](const sdc::ObjectRef& s) -> bool {
                if (s.kind == "get_cells") {
                    std::string cellPat = s.pattern;
                    std::replace(cellPat.begin(), cellPat.end(), '/', '.');
                    while (!cellPat.empty() && cellPat.back() == '.') cellPat.pop_back();
                    if (!cellPat.empty()) {
                        for (const auto& instPath : m->subInstancePaths) {
                            auto dot = instPath.find('.');
                            std::string rel = dot == std::string::npos ? instPath : instPath.substr(dot + 1);
                            if (globMatch(cellPat, instPath) || globMatch(cellPat, rel)) return true;
                        }
                    }
                    for (NodeId i = 0; i < nodeCount; ++i) {
                        if (m->flopOfQ.count(i) && rel_->mayNameMatch(s.kind, s.pattern, i) &&
                            (nameMatches(s.kind, s.pattern, i, stripBit(m->nameOf(i))) || bitMatches(s.kind, s.pattern, i)))
                            return true;
                    }
                    return false;
                }
                for (NodeId i = 0; i < nodeCount; ++i) {
                    if (rel_->mayNameMatch(s.kind, s.pattern, i) &&
                        (nameMatches(s.kind, s.pattern, i, stripBit(m->nameOf(i))) || bitMatches(s.kind, s.pattern, i))) return true;
                }
                return false;
            };
            for (auto& ck : c.clocks)
                for (auto& s : ck.sources) {
                    if (!refMatchesAnyNode(s)) {
                        add(mk("CST-002", Severity::Warning,
                               "clock constraint '" + ck.name + "' names '" + s.pattern + "' which resolves to no design object",
                               ck.loc, {}, Confidence::Unresolved));
                        incomplete("constraints", "constraint object '" + s.pattern + "' does not resolve");
                    }
                }
            for (const auto& ex : c.exceptions) {
                for (const auto* refs : {&ex.from, &ex.to, &ex.through}) {
                    for (const auto& s : *refs) {
                        if (s.regexp || s.unsupportedOption) continue;
                        if (s.kind == "get_clocks" || s.kind == "all_clocks" ||
                            s.kind == "all_inputs" || s.kind == "all_inputs -no_clocks" || s.kind == "all_outputs")
                            continue;
                        if (s.kind.empty() && std::any_of(latest.begin(), latest.end(), [&](const auto& entry) {
                                return globMatch(s.pattern, entry.first);
                            }))
                            continue;
                        if (!refMatchesAnyNode(s)) {
                            add(mk("CST-002", Severity::Warning,
                                   "path exception selector '" + s.pattern + "' resolves to no design object",
                                   ex.loc, {}, Confidence::Unresolved));
                            incomplete("constraints", "constraint selector '" + s.pattern + "' does not resolve");
                        }
                    }
                }
            }
        }
        // CST-018: clock references in set_clock_groups and path-exception [get_clocks ...] must resolve to declared clocks.
        auto declaredClockExists = [&](const std::string& pat) {
            return std::any_of(c.clocks.begin(), c.clocks.end(), [&](const sdc::ClockDef& ck) {
                const std::string name = ck.name.empty() && !ck.sources.empty() ? ck.sources.front().pattern : ck.name;
                return !name.empty() && globMatch(pat, name);
            });
        };
        for (const auto& g : c.clockGroups) {
            for (const auto& grp : g.groups) {
                for (const auto& ref : grp) {
                    if (!ref.kind.empty() && ref.kind != "get_clocks" && ref.kind != "all_clocks") {
                        problem("set_clock_groups requires clock selectors, not '" + ref.kind + " " + ref.pattern + "'", g.loc);
                        continue;
                    }
                    if (ref.regexp || ref.unsupportedOption || ref.kind == "all_clocks") continue;
                    if (!declaredClockExists(ref.pattern))
                        problem("set_clock_groups references undeclared clock '" + ref.pattern + "'", g.loc);
                }
            }
        }
        for (const auto& ex : c.exceptions) {
            for (const auto* refs : {&ex.from, &ex.to, &ex.through}) {
                for (const auto& ref : *refs) {
                    if (ref.kind == "get_clocks" && !ref.regexp && !ref.unsupportedOption &&
                        !declaredClockExists(ref.pattern))
                        problem("path exception references undeclared clock '" + ref.pattern + "'", ex.loc);
                }
            }
        }
    }

    std::vector<const sdc::OutputDelay*> outputContracts_;
    std::set<NodeId> boundaryEventInputs_;
    std::set<std::pair<NodeId, uint32_t>> boundaryEventLoss_;   // exact input and recognized first stage

    using ActiveInputContract = ClockRelations::ActiveInputContract;
    static std::string inputModesText(unsigned modes) {
        if (modes == 1u) return "min";
        if (modes == 2u) return "max";
        return "min/max";
    }
    static std::string inputContractText(const ActiveInputContract& contract) {
        const auto& delay = *contract.delay;
        return "effective " + inputModesText(contract.modes) + " mode, " +
            (delay.addDelay ? "additive" : "replacing") + " set_input_delay -clock " + delay.clock +
            " (" + delay.loc.file + ":" + std::to_string(delay.loc.line) + ")";
    }
    std::vector<ActiveInputContract> inputContractsFor(NodeId node, bool* rejectedMode = nullptr) const {
        return rel_->effectiveInputDelays(node, rejectedMode);
    }

    // The shared cone is intentionally a may-depend graph. For a boundary ERROR we
    // need at least one non-approximate path from the input to this particular D
    // cone; an all-to-all edge created by a call/dynamic select is only uncertainty.
    InputPath inputPath(NodeId input, NodeId capture) const {
        struct State { NodeId node; bool ctrl; bool approximate; };
        InputPath result;
        std::vector<State> pending{{capture, false, false}};
        std::set<std::tuple<NodeId, bool, bool>> seen;
        size_t work = 0;
        while (!pending.empty()) {
            const auto state = pending.back(); pending.pop_back();
            if (!seen.emplace(state.node, state.ctrl, state.approximate).second) continue;
            if (++work > 200000) { result.uncertain = true; break; }
            if (state.node == input) {
                if (state.approximate) result.uncertain = true;
                else if (state.ctrl) result.control = true;
                else result.data = true;
                continue;
            }
            if (state.node != capture && m->flopOfQ.count(state.node)) continue;
            for (auto ei : m->in[state.node]) {
                const auto& edge = m->edges[ei];
                if (edge.type != EdgeType::Comb && edge.type != EdgeType::Seq) continue;
                pending.push_back({edge.from, state.ctrl || edge.ctrl, state.approximate || edge.approximate});
            }
        }
        return result;
    }

    // The cone is a may-depend graph. Reject approximate output paths before
    // treating a particular synchronizer as observable.
    bool preciseBoundaryPath(NodeId bit, bool traceRegisterInput = false) const {
        std::vector<NodeId> pending{bit};
        std::unordered_set<NodeId> seen;
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            if (!seen.insert(node).second) continue;
            if (seen.size() > 200000) return false;
            if ((m->flopAt(node) && !(traceRegisterInput && node == bit)) || m->topInput[node]) continue;
            for (auto ei : m->in[node]) {
                const auto& edge = m->edges[ei];
                if (edge.type != EdgeType::Comb && edge.type != EdgeType::Seq) continue;
                if (edge.approximate) return false;
                pending.push_back(edge.from);
            }
        }
        return true;
    }

    SourceLocation outputLocation(NodeId bit) const {
        if (const auto* flop = m->flopAt(bit)) return procOf(*flop).loc;
        for (auto ei : m->in[bit]) {
            const auto& edge = m->edges[ei];
            if (edge.type == EdgeType::Comb && m->procs[edge.proc].loc.valid())
                return m->procs[edge.proc].loc;
        }
        return {};
    }

    void analyseObservedOutput(const std::string& name, const sdc::OutputDelay& decl,
                               const std::set<NodeId>& bits) {
        struct Delivery {
            std::string launchClock;
            Relation relation = Relation::Unknown;
            std::vector<NodeId> visibleBits;
        };
        std::map<NodeId, Delivery> deliveries;
        std::map<NodeId, NodeId> fields;
        std::set<NodeId> fieldSources;
        std::map<uint32_t, NodeId> exactOrigins;
        for (auto bit : bits) {
            if (m->collapsed(bit)) continue;
            const auto cone = cones_->ofNodes({bit});
            std::map<NodeId, NodeId> sources;
            bool logic = false;
            for (const auto& source : cone.flops) {
                const auto sync = syncedOut_.find(source.q);
                if (sync == syncedOut_.end() || !sync->second.full ||
                    rel_->relationToName(sync->second.dstDomain, decl.clock) != Relation::Same) continue;
                const auto& chain = sync->second;
                auto [origin, fresh] = exactOrigins.try_emplace(chain.head, kNoNode);
                if (fresh) {
                    const auto head = coneOf(m->flops[chain.head]);
                    if (head->flops.size() + head->inputs.size() == 1 && head->unresolved.empty() &&
                        head->bbLeaves.empty() && head->uncertainCalls.empty() && head->undriven.empty() &&
                        preciseBoundaryPath(m->flops[chain.head].q, true))
                        origin->second = head->inputs.empty() ? head->flops.front().q : head->inputs.front().q;
                }
                const auto launch = origin->second;
                if (launch == kNoNode || m->collapsed(launch)) continue;
                sources.try_emplace(launch, source.q);
                logic |= source.logic;
            }
            if (sources.empty()) continue;
            if (!cone.bbLeaves.empty() || !cone.unresolved.empty() || !cone.uncertainCalls.empty() ||
                !cone.undriven.empty() || !preciseBoundaryPath(bit)) {
                auto uncertain = mk("CST-012", Severity::Warning,
                    "observable synchronizer sources at output bit '" + m->nameOf(bit) +
                    "' are unresolved because the output dependency is imprecise",
                    outputLocation(bit), {}, Confidence::Unresolved);
                uncertain.signals = {m->nameOf(bit)};
                uncertain.identityKey = "output-precision:" + m->nameOf(bit) + ":" + decl.clock;
                uncertain.evidence.push_back({"sdc_command", decl.text, decl.loc});
                uncertain.nextStep = "Establish which synchronized sources actually reach this output bit.";
                uncertain.proofState = ProofState::NotAttempted;
                evaluated["CST-012"]++;
                add(std::move(uncertain));
                incomplete("graph-precision", "imprecise synchronized output dependency for '" + m->nameOf(bit) + "'");
                continue;
            }
            if (sources.size() == 1) {
                fields.emplace(bit, sources.begin()->first);
                fieldSources.insert(sources.begin()->first);
            }
            if (sources.size() > 1 && logic) {
                const auto bitName = m->nameOf(bit);
                auto reconverge = mk("CDC-010", Severity::Warning,
                    "output bit '" + bitName + "' combines separately synchronized influences under set_output_delay -clock " +
                    decl.clock + "; source correlation and external glitch tolerance are unresolved",
                    outputLocation(bit), {}, Confidence::Unresolved);
                reconverge.signals = {bitName};
                reconverge.identityKey = bitName + ":" + decl.clock;
                reconverge.evidence.push_back({"output_bit", bitName, reconverge.location});
                reconverge.evidence.push_back({"sdc_command", decl.text, decl.loc});
                for (const auto& [launch, stage] : sources)
                    reconverge.evidence.push_back({"synchronized_source_bit", m->nameOf(launch) + " via " + m->nameOf(stage),
                                                  procOf(*m->flopAt(stage)).loc});
                reconverge.evidence.push_back({"proof_obligation",
                    "establish source correlation or consumer tolerance of intermediate and glitch values", {}});
                reconverge.proofState = ProofState::NotAttempted;
                reconverge.nextStep = "Review source correlation and external capture behavior. If values must change atomically, provide a qualified transfer and protocol evidence.";
                evaluated["CDC-010"]++;
                add(std::move(reconverge));
            }
            for (const auto& [launch, stage] : sources) {
                if (!m->topInput[launch] || boundaryEventLoss_.count({launch, syncedOut_.at(stage).head})) continue;
                auto [entry, fresh] = deliveries.try_emplace(launch);
                auto& delivery = entry->second;
                if (fresh) {
                    bool rejectedMode = false;
                    const auto contracts = inputContractsFor(launch, &rejectedMode);
                    bool allExclusive = !rejectedMode && !contracts.empty();
                    for (const auto& active : contracts) {
                        const auto launchRelation = rel_->relationByNames(active.delay->clock, decl.clock);
                        allExclusive &= launchRelation == Relation::Exclusive;
                        if (launchRelation == Relation::Async) {
                            delivery.relation = Relation::Async;
                            delivery.launchClock = active.delay->clock;
                        }
                    }
                    if (allExclusive) delivery.relation = Relation::Exclusive;
                }
                if (delivery.relation != Relation::Exclusive) delivery.visibleBits.push_back(bit);
            }
        }
        if (fieldSources.size() > 1) {
            const auto firstBit = fields.begin()->first;
            auto coherence = mk("CST-012", Severity::Warning,
                "output word '" + name + "' packs independently synchronized source bits; field independence or coherence is unresolved",
                outputLocation(firstBit), {}, Confidence::Unresolved);
            coherence.signals = {name};
            coherence.identityKey = "word:" + name + ":" + decl.clock;
            coherence.evidence.push_back({"sdc_command", decl.text, decl.loc});
            for (const auto& [bit, launch] : fields)
                coherence.evidence.push_back({"output_word_bit", m->nameOf(bit) + " <= " + m->nameOf(launch), outputLocation(bit)});
            coherence.evidence.push_back({"proof_obligation",
                "declare field independence or establish the transfer and consumer contract for a coherent word", {}});
            coherence.proofState = ProofState::NotAttempted;
            coherence.nextStep = "Document field independence or provide protocol evidence that the external consumer observes a coherent value.";
            evaluated["CST-012"]++;
            add(std::move(coherence));
        }
        for (const auto& [launch, delivery] : deliveries) {
            if (delivery.visibleBits.empty()) continue;
            auto warning = mk("CDC-013", Severity::Warning,
                "synchronized primary input '" + m->nameOf(launch) + "' reaches clocked external output '" + name +
                (boundaryEventInputs_.count(launch) ? "'; delivery at the external observation remains unproved despite the input event contract"
                                                   : "' without a declared minimum duration or event-delivery requirement"),
                outputLocation(delivery.visibleBits.front()), {}, Confidence::Unresolved);
            warning.signals = {m->nameOf(launch), name};
            warning.identityKey = m->nameOf(launch) + ":" + name + ":" + decl.clock;
            Crossing crossing;
            crossing.launch.signal = m->nameOf(launch);
            crossing.launch.sdcClock = delivery.launchClock;
            crossing.capture.signal = name;
            crossing.capture.sdcClock = decl.clock;
            crossing.capture.location = warning.location;
            crossing.relationship = relationshipName(delivery.relation);
            crossing.widthBits = delivery.visibleBits.size();
            warning.crossing = std::move(crossing);
            warning.evidence.push_back({"sdc_command", decl.text, decl.loc});
            for (auto bit : delivery.visibleBits)
                warning.evidence.push_back({"output_bit", m->nameOf(bit), outputLocation(bit)});
            warning.evidence.push_back({"proof_obligation",
                boundaryEventInputs_.count(launch) ? "verify downstream event separation and delivery at the external observation"
                                                  : "state minimum assertion duration, event spacing, and whether every event must be delivered", {}});
            warning.proofState = ProofState::NotAttempted;
            warning.nextStep = "Obtain the interface delivery and duration contract and verify it against the synchronizer and external capture protocol.";
            evaluated["CDC-013"]++;
            add(std::move(warning));
        }
    }

    // Output cones stop at launch registers, including final synchroniser stages.
    // Synchronisation before that boundary says nothing about the external capture.
    void analyseOutputs() {
        evaluated["CST-012"] = 0;
        if (!cfg->constraintsProvided && outputContracts_.empty()) return;
        struct Observation {
            const sdc::OutputDelay* first = nullptr;
            std::set<NodeId> bits;
        };
        std::map<std::string, std::vector<NodeId>> outputs;
        for (NodeId n = 0; n < m->nodes.size(); ++n)
            if (m->topOutput[n]) outputs[stripBit(m->nameOf(n))].push_back(n);
        for (const auto& [name, bits] : outputs) {
            std::map<std::string, Observation> observations;
            std::set<NodeId> contractedBits;
            for (const auto* decl : outputContracts_) {
                std::vector<NodeId> matched;
                for (auto n : bits)
                    for (const auto& ref : decl->ports)
                        if (const auto kind = ref.kind.empty() ? "get_ports" : ref.kind;
                            nameMatches(kind, ref.pattern, n, name) || bitMatches(kind, ref.pattern, n)) {
                            matched.push_back(n);
                            break;
                        }
                if (matched.empty()) continue;
                contractedBits.insert(matched.begin(), matched.end());
                evaluated["CST-012"] += matched.size();
                auto cone = cones_->ofNodes(matched);
                if (std::any_of(cone.flops.begin(), cone.flops.end(),
                                [&](const ConeSrc& source) { return syncedOut_.contains(source.q); })) {
                    auto& observation = observations[decl->clock];
                    if (!observation.first) observation.first = decl;
                    observation.bits.insert(matched.begin(), matched.end());
                }

                for (const auto& input : cone.inputs) {
                    bool launchRelated = false, asynchronousLaunch = false, unknownLaunch = false;
                    bool rejectedMode = false;
                    std::vector<Evidence> contractEvidence;
                    for (const auto& contract : inputContractsFor(input.q, &rejectedMode)) {
                        const auto& inputDelay = *contract.delay;
                        const Relation inputRelation = rel_->relationByNames(inputDelay.clock, decl->clock);
                        launchRelated |= inputRelation == Relation::Same;
                        asynchronousLaunch |= inputRelation == Relation::Async;
                        unknownLaunch |= inputRelation == Relation::Unknown || inputRelation == Relation::Exclusive;
                        contractEvidence.push_back({"input_delay_contract", inputContractText(contract), inputDelay.loc});
                    }
                    unknownLaunch |= rejectedMode;
                    if (launchRelated && !asynchronousLaunch && !unknownLaunch) continue;
                    const Severity inputSeverity = asynchronousLaunch ? Severity::Error : Severity::Warning;
                    Diagnostic dcdc = mk("CDC-029", inputSeverity,
                        "top-level input '" + m->nameOf(input.q) + "' feeds output '" + name +
                        "' with set_output_delay -clock " + decl->clock + " without a registering or synchronizing element" +
                        (asynchronousLaunch ? "; at least one active input-delay launch clock is declared asynchronous to the output capture clock"
                         : launchRelated ? "; active input-delay contracts mix Same with an unknown or exclusive relationship"
                                         : "; no active same-clock input-delay contract establishes its launch"),
                        decl->loc, {}, asynchronousLaunch ? Confidence::Established : Confidence::Unresolved);
                    dcdc.evidence.insert(dcdc.evidence.end(), contractEvidence.begin(), contractEvidence.end());
                    if (asynchronousLaunch && unknownLaunch)
                        dcdc.evidence.push_back({"mixed_clock_intent", "an active asynchronous launch contract establishes the crossing while another effective min/max contract has an unknown or exclusive relationship", {}});
                    dcdc.signals = {name, stripBit(m->nameOf(input.q))};
                    dcdc.identityKey = stripBit(m->nameOf(input.q)) + ":" + name + ":" + decl->clock;
                    Crossing boundary;
                    boundary.capture.signal = name;
                    boundary.capture.sdcClock = decl->clock;
                    boundary.relationship = asynchronousLaunch ? "ASYNC" : "UNKNOWN";
                    boundary.widthBits = matched.size();
                    dcdc.crossing = std::move(boundary);
                    dcdc.nextStep = "Declare the external launch clock with set_input_delay or register the signal in the intended domain.";
                    add(std::move(dcdc));
                    evaluated["CDC-029"]++;
                    if (!asynchronousLaunch || unknownLaunch)
                        incomplete("constraints", "primary-input feed-through to output '" + name + "' has an unknown or mixed launch relationship");
                }
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
                        Relation r = f.clockResolved ? rel_->relationToName(launchDomain, decl->clock) : Relation::Unknown;
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
                if (uncertain) incomplete("constraints", "unresolved output-boundary contract for '" + name + "'");
                add(std::move(d));
            }
            for (const auto& [clock, observation] : observations)
                analyseObservedOutput(name, *observation.first, observation.bits);
            std::vector<NodeId> uncontractedBits;
            for (auto n : bits)
                if (!contractedBits.count(n)) uncontractedBits.push_back(n);
            if (!uncontractedBits.empty() && cfg->constraintsProvided) {
                auto cone = cones_->ofNodes(uncontractedBits);
                bool hasSynced = false;
                std::set<uint32_t> outDomains;
                for (const auto& src : cone.flops) {
                    if (syncedOut_.contains(src.q)) hasSynced = true;
                    if (const auto* f = m->flopAt(src.q); f && f->clockResolved)
                        for (auto dom : domainsOf(*f)) outDomains.insert(dom);
                }
                if (!hasSynced && outDomains.size() == 1 && !contractedBits.empty()) {
                    auto fullCone = cones_->ofNodes(bits);
                    for (const auto& src : fullCone.flops)
                        if (const auto* f = m->flopAt(src.q); f && f->clockResolved)
                            for (auto dom : domainsOf(*f)) outDomains.insert(dom);
                }
                if (hasSynced || outDomains.size() > 1) {
                    evaluated["CST-012"] += uncontractedBits.size();
                    Diagnostic d = mk("CST-012", Severity::Warning,
                                      "top-level output '" + name + "' carries a " +
                                      (hasSynced ? "synchronized" : "multi-domain") +
                                      " CDC-relevant value without a complete set_output_delay capture contract: external capture clock and protocol are unresolved",
                                      outputLocation(uncontractedBits.front()), {}, Confidence::Unresolved);
                    d.signals = {name};
                    d.identityKey = "uncontracted:" + name;
                    d.proofState = ProofState::NotAttempted;
                    d.nextStep = "Declare set_output_delay -clock <capture_clock> for every CDC-relevant bit of this output or document the external capture boundary.";
                    add(std::move(d));
                    incomplete("constraints", "CDC-relevant top-level output '" + name + "' has uncontracted output bit(s)");
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
            incomplete("clock-extraction", "unidentified clock in sequential block at " +
                                   m->procs[proc].loc.file + ":" + std::to_string(m->procs[proc].loc.line));
        }
        std::map<uint32_t, uint32_t> perDomain;
        for (auto& f : m->flops) if (f.clockResolved) perDomain[f.domain]++;
        for (size_t i = 0; i < m->domains.size(); i++) {
            auto& dom = m->domains[i];
            const bool generatedByFlop = dom.kind == RootKind::Undriven && m->flopAt(dom.root);
            const char* kind = dom.kind == RootKind::TopPort ? "top-level port" : dom.kind == RootKind::Logic || generatedByFlop ? "logic-generated"
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
                incomplete("clock-extraction", std::string("clock '") + dom.name + "' is " + kind);
            }
            if (domainClockName(i).empty()) {
                Diagnostic g = mk("CST-011", Severity::Warning,
                                  "clock '" + dom.name + "' (" + std::to_string(perDomain[i]) + " register bit(s)) has no clock constraint",
                                  {}, {}, Confidence::Unresolved);
                g.identityKey = dom.name;
                g.nextStep = "Add create_clock for it (--sdc); until then every crossing involving it is UNKNOWN, not safe.";
                add(std::move(g));
                incomplete("constraints", "no constraint for clock '" + dom.name + "'");
            }
        }
    }

    // ------------------------------------------------------------ RDC-001 / CST-001
    void analyseResets() {
        evaluated["RDC-001"] = m->flops.size();
        evaluated["CST-001"] = m->flops.size();
        struct Rs { Root root; bool activeLow = false; std::set<uint32_t> domains; std::vector<uint32_t> users; uint64_t regs = 0, clears = 0, sets = 0, other = 0; SourceLocation loc; };
        std::map<NodeId, Rs> resets;
        uint64_t asyncRegs = 0, syncCand = 0, none = 0;
        std::map<uint32_t, std::array<uint64_t, 3>> perDomain;   // async, sync-candidate, none
        for (uint32_t fi = 0; fi < m->flops.size(); fi++) {
            auto& f = m->flops[fi];
            uint32_t dm = f.clockResolved ? f.domain : ~0u;
            if (!f.resets.empty()) {
                asyncRegs++;
                if (dm != ~0u) perDomain[dm][0]++;
                for (size_t ri = 0; ri < f.resets.size(); ri++) {
                    auto& r = f.resets[ri];
                    auto& e = resets[r.root.node];
                    e.root = r.root;
                    e.activeLow = r.activeLow;
                    e.users.push_back(fi);
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
        int rdcMinStages = 2;
        try {
            const std::string value = cfg->option("RDC-006.min_stages", "2");
            size_t end = 0;
            const int parsed = std::stoi(value, &end);
            if (end == value.size() && parsed >= 2) rdcMinStages = parsed;
        } catch (...) {}
        const auto assertedUses = rdc::assertedUsesOf(*m);
        auto copyRoot = [&](NodeId n) {
            for (int ch = 0; ch < 16; ++ch) {
                if (n == kNoNode || n >= m->in.size() || m->in[n].size() != 1) break;
                const Edge& e = m->edges[m->in[n][0]];
                if (e.type != EdgeType::Comb || e.ctrl || e.approximate || e.shared || !(e.exact || e.flip)) break;
                n = e.from;
            }
            return n;
        };
        auto rawSourcesAreTopPorts = [&](uint32_t firstFlop) {
            // Trace every raw reset source of the synchroniser's first stage through exact/inverted copies and recognised synchroniser hops.
            if (m->flops[firstFlop].resets.empty()) return false;
            std::vector<std::pair<NodeId, int>> work;
            for (const auto& res : m->flops[firstFlop].resets) work.emplace_back(res.root.node, 0);
            std::unordered_set<NodeId> seen;
            while (!work.empty()) {
                auto [cur, hop] = work.back();
                work.pop_back();
                if (!seen.insert(cur).second) return false;
                const NodeId copied = copyRoot(cur);
                const Root root = m->traceRoot(copied);
                if (root.kind == RootKind::TopPort) continue;
                if (root.kind != RootKind::Logic || hop >= 64) return false;
                uint32_t ri = 0;
                const Flop* rf = m->flopBehind(root.node, &ri);
                if (!rf) return false;
                const rdc::SyncInfo si = rdc::syncChainOf(*m, assertedUses, ri);
                if (!si.recognised || si.first == ~0u || m->flops[si.first].resets.empty()) return false;
                for (const auto& res : m->flops[si.first].resets) work.emplace_back(res.root.node, hop + 1);
            }
            return true;
        };
        auto isRecognisedTopPortSync = [&](NodeId node, const Rs& r, rdc::SyncInfo* outSi = nullptr) {
            if (r.root.kind != RootKind::Logic) return false;
            uint32_t ri = 0;
            const Flop* rf = m->flopBehind(node, &ri);
            if (!rf || !rf->clockResolved) return false;
            const rdc::SyncInfo si = rdc::syncChainOf(*m, assertedUses, ri);
            if (!si.recognised || si.depth < rdcMinStages || si.first == ~0u) return false;
            for (uint32_t ui : r.users) {
                const Flop& uf = m->flops[ui];
                if (!uf.clockResolved || rel_->relation(rf->domain, uf.domain) != Relation::Same || uf.edge != rf->edge)
                    return false;
            }
            if (!rawSourcesAreTopPorts(si.first)) return false;
            if (outSi) *outSi = si;
            return true;
        };
        std::unordered_set<uint32_t> recognisedSyncStages;
        for (const auto& [node, r] : resets) {
            const char tiedVal = rdc::tiedResetValue(*m, node);
            if (tiedVal == '0' || tiedVal == '1') continue;
            rdc::SyncInfo si;
            if (isRecognisedTopPortSync(node, r, &si))
                for (uint32_t st : si.stages) recognisedSyncStages.insert(st);
        }
        for (auto& [node, r] : resets) {
            const char tiedVal = rdc::tiedResetValue(*m, node);
            const bool isTied = tiedVal == '0' || tiedVal == '1';
            const bool isTiedActive = isTied && (r.activeLow ? tiedVal == '0' : tiedVal == '1');
            const bool isTiedInactive = isTied && !isTiedActive;
            const bool isSync = !isTied && isRecognisedTopPortSync(node, r);
            const NodeId copiedNode = copyRoot(node);
            const bool isSyncInput = !isTied && !isSync && r.root.kind == RootKind::Logic &&
                                     copiedNode < m->topInput.size() && m->topInput[copiedNode] &&
                                     !r.users.empty() &&
                                     std::all_of(r.users.begin(), r.users.end(), [&](uint32_t ui) { return recognisedSyncStages.count(ui) > 0; });
            const bool isBlackBoxPort = r.root.kind == RootKind::BlackBox && node < m->topInput.size() && m->topInput[node];
            bool isReleaseFlop = false;
            if (!isTied && !isSync && !isSyncInput && r.root.kind == RootKind::Logic) {
                uint32_t ri = 0;
                isReleaseFlop = m->flopBehind(node, &ri) && rdc::isReleaseFlop(*m, assertedUses, rdcMinStages, ri);
            }
            const char* kind = isTiedInactive ? "tied inactive"
                             : isTiedActive ? "tied active"
                             : isSync ? "synchroniser"
                             : isSyncInput ? "inverted top-level port feeding a synchroniser"
                             : isReleaseFlop ? "release flop feeding reset synchronisers"
                             : r.root.kind == RootKind::TopPort ? "top-level port" : r.root.kind == RootKind::Logic ? "logic-generated"
                             : isBlackBoxPort ? "connected to an unmodeled black box (port direction unknown)"
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
            if (isTiedActive) {
                const bool anyConstantForce = r.clears > 0 || r.sets > 0;
                const bool mismatchOrUndecided = r.other > 0;
                if (!anyConstantForce && mismatchOrUndecided) {
                    Diagnostic g = mk("RDC-001", Severity::Warning,
                                      "reset '" + m->nameOf(node) + "' is tied to its edge-active level (" + std::string(1, tiedVal) +
                                      "), but the tie level does not match the tested level: forced reset value is undecided for its " +
                                      std::to_string(r.regs) + " register bit(s)",
                                      r.loc, {}, Confidence::Unresolved);
                    g.identityKey = "tied-active:" + m->nameOf(node);
                    g.signals = {m->nameOf(node)};
                    g.nextStep = "Check the reset condition polarity against the event edge and constant tie.";
                    add(std::move(g));
                    incomplete("reset-extraction", "reset '" + m->nameOf(node) + "' tie level does not match tested level");
                } else if (mismatchOrUndecided) {
                    Diagnostic g = mk("RDC-001", Severity::Warning,
                                      "reset '" + m->nameOf(node) + "' is tied to its active level (" + std::string(1, tiedVal) +
                                      "): " + std::to_string(r.clears + r.sets) + " register bit(s) are held permanently in reset, while " +
                                      std::to_string(r.other) + " bit(s) have a tie level that does not match the tested level or an undecided reset value",
                                      r.loc, {}, Confidence::Unresolved);
                    g.identityKey = "tied-active:" + m->nameOf(node);
                    g.signals = {m->nameOf(node)};
                    g.nextStep = "Verify whether the constant tie is intentional and check the reset condition polarity against the event edge.";
                    add(std::move(g));
                    incomplete("reset-extraction", "reset '" + m->nameOf(node) + "' has register bit(s) whose tie level does not match tested level");
                } else {
                    Diagnostic g = mk("RDC-001", Severity::Warning,
                                      "reset '" + m->nameOf(node) + "' is tied to its active level (" + std::string(1, tiedVal) +
                                      "): its " + std::to_string(r.regs) + " register bit(s) are held permanently in reset",
                                      r.loc, {}, Confidence::Established);
                    g.identityKey = "tied-active:" + m->nameOf(node);
                    g.signals = {m->nameOf(node)};
                    g.nextStep = "Verify whether the constant tie is intentional; an active-level tie keeps the register(s) permanently in reset.";
                    add(std::move(g));
                }
            } else if (!isTiedInactive && r.root.kind != RootKind::TopPort && !isSync && !isSyncInput && !isReleaseFlop) {
                const std::string warnMsg = isBlackBoxPort
                    ? "reset '" + m->nameOf(node) + "' is a primary input also connected to an unmodeled black box (port direction unknown): its source and release synchronisation are unresolved"
                    : "reset '" + m->nameOf(node) + "' is " + kind + ", not a primary input: its source and release synchronisation are unresolved";
                Diagnostic g = mk("RDC-001", Severity::Warning, warnMsg, r.loc, {}, Confidence::Unresolved);
                g.identityKey = "unresolved:" + m->nameOf(node);
                g.nextStep = "Check the reset generation/synchroniser; RDC analysis of this reset is incomplete.";
                add(std::move(g));
                incomplete("reset-extraction", "reset '" + m->nameOf(node) + "' is " + kind);
            }
        }
        {   // synchronous reset candidates (heuristic: top-level if on one signal, constant assignments), polarity from the condition shape
            struct Sr { uint64_t regs = 0; SourceLocation loc; };
            std::map<SyncResetGuard, Sr> syncResets;
            for (auto& f : m->flops) if (f.resets.empty() && f.hasSyncResetCandidate) {
                std::set<SyncResetGuard> seen;
                for (const auto& fact : f.syncResetFacts) seen.insert(fact);
                if (seen.empty() && f.syncReset != kNoNode) seen.insert({f.syncReset, f.syncActiveLow, '?'});
                for (const auto& key : seen) {
                    auto& e = syncResets[key];
                    e.regs++;
                    if (!e.loc.valid()) e.loc = procOf(f).loc;
                }
            }
            for (auto& [fact, e] : syncResets) {
                const NodeId node = fact.node;
                const bool activeLow = fact.activeLow;
                const std::string forced = fact.forcedValue == '0' ? "clears" : fact.forcedValue == '1' ? "sets" : "assigns";
                Diagnostic d = mk("RDC-001", Severity::Info,
                                  "synchronous reset candidate '" + m->nameOf(node) + "' (" + std::string(activeLow ? "active-low" : "active-high") + " by condition shape) " + forced + ": " +
                                  std::to_string(e.regs) + " register bit(s) assigned this value when it is asserted",
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
            incomplete("clock-extraction", std::to_string(unk) + " register bit(s) without an identified clock");
        }
    }

    // Prefer the most recent definition, as clock-root resolution does.
    const sdc::ClockDef* activeBoundaryClock(const std::string& name) const {
        for (auto it = cfg->constraints.clocks.rbegin(); it != cfg->constraints.clocks.rend(); ++it) {
            const auto declared = it->name.empty() && !it->sources.empty() ? it->sources.front().pattern : it->name;
            if (declared == name) return &*it;
        }
        return nullptr;
    }

    std::string boundaryInputName(NodeId input) const {
        return rel_->boundaryInputName(input);
    }

    // The duration at the port reaches the sampling flop unchanged only
    // through a single unguarded exact path of wires and one sequential copy.
    bool directBoundarySample(NodeId input, const Flop& sample, const Chain& chain, std::string* failure = nullptr) const {
        auto reject = [&](const char* why) { if (failure) *failure = why; return false; };
        if (sample.blocking || !sample.clockResolved || (sample.edge != 1 && sample.edge != 2) ||
            m->multiSequential.count(sample.q) || !sample.otherDomains.empty() || m->modeConstants.count(input) ||
            chain.resetMismatch)
            return reject("sampler is not one resolved, unconditional single-edge domain");
        const bool hasReset = !sample.resets.empty();
        if (hasReset && (sample.resets.size() != 1 || !sample.resetTransitionKnown ||
            sample.releasedSource == kNoNode || !sample.resets[0].async || !sample.resets[0].activeLow ||
            !sample.resets[0].fromEdge || sample.resetValues.size() != 1 ||
            (sample.resetValues[0] != '0' && sample.resetValues[0] != '1')))
            return reject("reset is not one exact active-low async reset with a constant asserted value and released source");
        if (hasReset) {
            const auto resetRoot = m->traceRoot(sample.resets[0].signal);
            if (resetRoot.kind != RootKind::TopPort)
                return reject("reset is not one unconstrained top-level input");
            for (auto node : {resetRoot.node, sample.resets[0].signal}) {
                const auto name = stripBit(m->nameOf(node));
                const auto bitName = boundaryInputName(node);
                const bool caseConstrained = std::any_of(cfg->constraints.caseAnalyses.begin(),
                    cfg->constraints.caseAnalyses.end(), [&](const auto& mode) {
                        return std::any_of(mode.objects.begin(), mode.objects.end(), [&](const auto& object) {
                            const auto kind = object.kind.empty() ? "get_ports" : object.kind;
                            return nameMatches(kind, object.pattern, node, name) ||
                                   rel_->bitMatches(kind, object.pattern, node, bitName);
                        });
                    });
                const bool clockRoot = std::any_of(m->domains.begin(), m->domains.end(),
                                                  [&](const auto& domain) { return domain.root == node; });
                bool rejectedMode = false;
                const auto inputDelays = inputContractsFor(node, &rejectedMode);
                if (clockRoot || rel_->isDeclaredClock(node) || m->modeConstants.count(node) ||
                    caseConstrained || !inputDelays.empty() || rejectedMode)
                    return reject("reset has clock, data timing, or mode intent that prevents this sampling assumption");
            }
        }
        NodeId node = sample.q;
        for (unsigned depth = 0; depth < 32; ++depth) {
            if (node == input) return true;
            if (m->blackBoxDriven[node] || m->unresolvedDriven[node] ||
                m->uncertainCallDrivers.count(node) || (m->constDrivers.count(node) && !(depth == 0 && hasReset)))
                return reject("source path is opaque, unresolved, or constant-driven");
            const Edge* dataEdge = nullptr;
            bool sawResetEdge = false;
            for (auto ei : m->in[node]) {
                const auto& candidate = m->edges[ei];
                if (candidate.type == EdgeType::AsyncCtl && depth == 0 && hasReset &&
                    candidate.from == sample.resets[0].signal && !sawResetEdge) {
                    sawResetEdge = true;
                    continue;
                }
                if (candidate.type != (depth == 0 ? EdgeType::Seq : EdgeType::Comb) || dataEdge)
                    return reject("source path is not one exact unconditional data edge: extra writer, reset, or non-data edge");
                dataEdge = &candidate;
            }
            if (!dataEdge || (depth == 0 && hasReset && !sawResetEdge))
                return reject("exact data edge or expected asynchronous reset edge is missing");
            const auto& edge = *dataEdge;
            if ((depth == 0 && hasReset && edge.from != sample.releasedSource) || !edge.exact || edge.ctrl || edge.approximate ||
                edge.type != (depth == 0 ? EdgeType::Seq : EdgeType::Comb))
                return reject("source path is guarded, approximate, or not an exact bit move");
            node = edge.from;
        }
        return reject("source path exceeds the supported depth");
    }

    bool observableBoundaryChain(const Chain& chain) const {
        std::vector<NodeId> aliases;
        aliasForward(m->flops[chain.stages.back()].q, aliases);
        for (auto node : aliases) {
            if (m->topOutput[node]) return true;
            for (auto ei : m->out[node]) {
                const auto& edge = m->edges[ei];
                if (edge.type != EdgeType::Comb || !edge.exact) return true;
            }
        }
        return false;
    }

    void analyseBoundaryEvents(const std::map<std::pair<NodeId, uint32_t>, std::vector<uint32_t>>& captures) {
        if (cfg->boundaryEvents.empty()) return;
        std::map<std::string, std::vector<NodeId>> ports;
        for (const auto& contract : cfg->boundaryEvents) ports.try_emplace(contract.port);
        for (NodeId node = 0; node < m->nodes.size(); ++node) {
            if (!m->topInput[node]) continue;
            const auto full = boundaryInputName(node);
            if (full.empty()) continue;
            if (auto match = ports.find(full); match != ports.end()) match->second.push_back(node);
            if (const auto dot = full.rfind('.'); dot != std::string::npos)
                if (auto match = ports.find(full.substr(dot + 1)); match != ports.end()) match->second.push_back(node);
        }
        std::map<NodeId, std::vector<const Config::BoundaryEventContract*>> bound;
        auto evidenceText = [](const Config::BoundaryEventContract& contract) {
            std::ostringstream text;
            text.precision(std::numeric_limits<double>::max_digits10);
            text << contract.port << ": " << contract.protocol << ", " << contract.duration << ' ' << contract.durationUnit
                 << ", delivery=" << contract.delivery << ", launch_clock=" << contract.launchClock;
            return text.str();
        };
        auto unapplied = [&](const Config::BoundaryEventContract& contract, const std::string& reason) {
            auto d = mk("CDC-013", Severity::Warning, "boundary event contract for '" + contract.port + "' is unapplied: " + reason,
                        {}, {}, Confidence::Unresolved);
            d.signals = {contract.port};
            d.identityKey = "boundary-event:" + evidenceText(contract) + ":" + reason;
            d.evidence.push_back({"boundary_event_contract", evidenceText(contract), {}});
            d.evidence.push_back({"boundary_contract_validation", reason, {}});
            d.proofState = ProofState::NotAttempted;
            evaluated["CDC-013"]++;
            add(std::move(d));
            incomplete("boundary-event-intent", reason + " for '" + contract.port + "'");
        };
        for (const auto& contract : cfg->boundaryEvents) {
            const auto& matches = ports.at(contract.port);
            if (matches.size() != 1) {
                unapplied(contract, "port must resolve to one exact supported primary-input bit");
                continue;
            }
            bound[matches.front()].push_back(&contract);
            boundaryEventInputs_.insert(matches.front());
        }
        static const std::map<std::string, double> unitScale = {{"s", 1e9}, {"ms", 1e6}, {"us", 1e3},
            {"ns", 1}, {"ps", 1e-3}, {"fs", 1e-6}};
        for (const auto& [input, contracts] : bound) {
            if (contracts.size() != 1) {
                for (const auto* contract : contracts) unapplied(*contract, "multiple contracts bind the same input bit");
                continue;
            }
            const auto& contract = *contracts.front();
            const auto bitName = boundaryInputName(input);
            const auto* launch = activeBoundaryClock(contract.launchClock);
            bool rejectedMode = false;
            const auto inputDelays = inputContractsFor(input, &rejectedMode);
            const bool clockConsistent = !rejectedMode && std::all_of(inputDelays.begin(), inputDelays.end(), [&](const auto& active) {
                return active.delay->clock == contract.launchClock;
            });
            const auto unit = unitScale.find(contract.durationUnit);
            const double scale = unit != unitScale.end() ? unit->second :
                contract.durationUnit == "source_cycles" && launch ? launch->periodNs : 0;
            const double duration = contract.duration * scale;
            std::string issue;
            if (contract.protocol != "event" && contract.protocol != "level") issue = "unsupported protocol";
            else if (contract.delivery != "required" && contract.delivery != "best_effort") issue = "unsupported delivery policy";
            else if (!launch) issue = "launch clock is not declared by SDC";
            else if (!clockConsistent) issue = "contract launch clock conflicts with effective set_input_delay";
            else if (!(std::isfinite(duration) && duration > 0)) issue = "duration conversion requires finite positive time";
            if (!issue.empty()) {
                unapplied(contract, issue);
                continue;
            }
            bool recognized = false;
            for (auto it = captures.lower_bound({input, 0}); it != captures.end() && it->first.first == input; ++it) {
                const auto domain = it->first.second;
                const double interval = periodNs(domain);
                const auto relation = rel_->hasMultipleClocks(domain) ? Relation::Unknown :
                    rel_->relationByNames(contract.launchClock, domainClockName(domain), true);
                for (auto capture : it->second) {
                    const auto& chain = chains_[capture];
                    if (chain.linkLogic || chain.feedbackWithFanout || chain.depth < minStages_) continue;
                    recognized = true;
                    const auto& sample = m->flops[capture];
                    std::string reason;
                    if (relation != Relation::Async) reason = "launch is not established asynchronous to the sampler";
                    else if (!(std::isfinite(interval) && interval > 0)) reason = "destination period is not finite and positive";
                    else if (chain.dualEdgeStage) reason = "dual-edge synchronizer timing is unresolved";
                    else if (chain.clockMismatch) reason = "synchronizer stages use different clock edges";
                    else if (!observableBoundaryChain(chain)) reason = "recognized chain has no modeled downstream consumer";
                    else directBoundarySample(input, sample, chain, &reason);
                    const bool applicable = reason.empty();
                    const bool shorter = applicable && interval - duration >
                        4 * std::numeric_limits<double>::epsilon() * std::max(interval, duration);
                    const bool loss = shorter && contract.delivery == "required";
                    if (applicable && !shorter && contract.delivery == "required" && !sample.resets.empty())
                        reason = "assertion spans the sampling interval with reset deasserted, but the contract does not guarantee sampling outside reset";
                    else if (applicable && !shorter && contract.delivery == "required" && duration / interval < 1.5)
                        reason = "assertion spans one ideal sampling interval but falls below the 1.5-period review margin";
                    const bool unresolved = !reason.empty();
                    const std::string message = unresolved ? "boundary event contract for '" + bitName + "' is unresolved: " + reason :
                        loss ? "asynchronous primary input '" + bitName + "' has a permitted assertion shorter than one destination sampling interval; required event delivery can be lost at arbitrary phase" :
                        shorter ? "asynchronous primary input '" + bitName + "' permits a short assertion under a best-effort delivery policy" :
                        "asynchronous primary input '" + bitName + "' has a boundary duration at least one destination sampling interval; event separation and protocol delivery remain unproved";
                    auto d = mk("CDC-013", unresolved || loss ? Severity::Warning : Severity::Info, message,
                                procOf(sample).loc, procOf(sample).instancePath,
                                unresolved ? Confidence::Unresolved : loss ? Confidence::Established : Confidence::Heuristic);
                    const auto details = evidenceText(contract);
                    std::ostringstream sampling;
                    sampling.precision(std::numeric_limits<double>::max_digits10);
                    sampling << duration << " ns asserted; destination period ";
                    if (std::isfinite(interval) && interval > 0) sampling << interval << " ns"; else sampling << "unknown";
                    d.signals = {bitName, m->nameOf(sample.q)};
                    d.identityKey = "boundary-event:" + bitName + ":" + flopName(sample) + ":" + domainClockName(domain) +
                        ":" + details + ":" + sampling.str() + ":" + relName(relation) + ":" + reason;
                    Crossing crossing;
                    crossing.launch.signal = bitName;
                    crossing.launch.sdcClock = contract.launchClock;
                    crossing.capture = endpoint(sample, flopName(sample), domain);
                    crossing.relationship = relationshipName(relation);
                    crossing.widthBits = 1;
                    d.crossing = std::move(crossing);
                    d.evidence.push_back({"boundary_event_contract", details, {}});
                    d.evidence.push_back({"boundary_contract_validation", unresolved ? reason : "contract applies to this exact input bit and synchronizer", {}});
                    d.evidence.push_back({"sampling_intervals", sampling.str(), {}});
                    const std::string trust = "duration, delivery and clock intent are taken from the supplied interface contract and SDC";
                    d.evidence.push_back({"trust_model", applicable && !sample.resets.empty() ?
                        trust + "; the interval comparison applies during reset-deasserted operation" : trust, {}});
                    d.evidence.push_back({"physical_scope", sample.resets.empty() ?
                        "ideal RTL sampling only; physical timing, event spacing and protocol closure require separate evidence" :
                        "ideal RTL sampling only; reset deassertion timing, recovery/removal, metastability, physical timing, event spacing and protocol closure require separate evidence", {}});
                    d.proofState = ProofState::NotAttempted;
                    d.nextStep = "Verify source duration and event spacing; use a retained event mechanism where short events must be delivered.";
                    evaluated["CDC-013"]++;
                    add(std::move(d));
                    if (unresolved) incomplete("boundary-event-intent", reason + " for '" + bitName + "'");
                    if (loss) boundaryEventLoss_.emplace(input, capture);
                }
            }
            if (!recognized) unapplied(contract, "no recognized synchronizer of the required depth consumes this bit");
        }
    }

    // ------------------------------------------------------------ crossings
    // forward closure over exact (alias) combinational edges; never treat a net
    // with competing live drivers as an exact alias.
    void aliasForward(NodeId start, std::vector<NodeId>& closure) const {
        std::unordered_set<NodeId> seen{start};
        closure.push_back(start);
        for (size_t i = 0; i < closure.size(); i++)
            for (auto ei : m->out[closure[i]]) {
                auto& e = m->edges[ei];
                if (e.type != EdgeType::Comb || !e.exact || e.ctrl || e.approximate ||
                    m->isDeadEdge(e) || m->isConstantProc(e.proc)) continue;
                if (m->constDrivers.contains(e.to)) continue;
                size_t liveIn = 0;
                bool exactSingle = true;
                for (auto inEi : m->in[e.to]) {
                    const auto& inE = m->edges[inEi];
                    if (m->isDeadEdge(inE)) continue;
                    ++liveIn;
                    if (inE.type != EdgeType::Comb || inE.from != closure[i] || !inE.exact ||
                        inE.ctrl || inE.approximate)
                        exactSingle = false;
                }
                if (liveIn != 1 || !exactSingle) continue;
                if (seen.insert(e.to).second) closure.push_back(e.to);
            }
    }

    // Shared reverse reachability lets each chain stop as soon as adjacent
    // stages meet anywhere in an exact cone with an observable data consumer.
    // Without this index, a shared reduction's wide output fanout was walked
    // again for every input bit merely to find its first output consumer.
    void prepareAdjacentConsumers() const {
        if (adjacentConsumerIndexReady_) return;
        adjacentConsumerIndexReady_ = true;
        adjacentConsumerFor_.assign(m->nodes.size(), kNoNode);
        std::vector<NodeId> pending;
        auto seed = [&](NodeId node, NodeId consumer) {
            if (node >= adjacentConsumerFor_.size() || adjacentConsumerFor_[node] != kNoNode) return;
            adjacentConsumerFor_[node] = consumer;
            pending.push_back(node);
        };
        for (NodeId node = 0; node < m->topOutput.size(); ++node)
            if (m->topOutput[node]) seed(node, node);
        for (const auto& edge : m->edges) {
            if (edge.type == EdgeType::Seq && !edge.approximate && !m->isDeadEdge(edge) && !m->isConstantProc(edge.proc)) {
                if (const Flop* dstFlop = m->flopAt(edge.to); dstFlop && dstFlop->blocking) continue;
                seed(edge.from, edge.to);
            }
        }
        for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
            const NodeId node = pending[cursor];
            if (m->flopOfQ.count(node)) continue;
            for (auto ei : m->in[node]) {
                const auto& edge = m->edges[ei];
                if (edge.type == EdgeType::Comb && !edge.approximate && !m->isDeadEdge(edge) && !m->isConstantProc(edge.proc))
                    seed(edge.from, adjacentConsumerFor_[node]);
            }
        }
    }

    // A shift-word observation is positive structural evidence only when
    // distinct effective stage cycle depths reach distinct top-level output
    // bits through exact aliases, directly or through one unconditional exact
    // same-domain capture. Combinationally processed outputs and same-cycle
    // aliases do not count.
    bool observableShiftWord(const Chain& chain, std::vector<std::string>* evidence = nullptr,
                             size_t* mappingCount = nullptr) const {
        using PortBits = std::map<std::string, std::map<NodeId, std::set<int>>>;
        PortBits observedStages;
        std::map<std::pair<std::string, NodeId>, std::pair<int, std::string>> sourceNames;
        std::vector<int> stageCycle(chain.stages.size(), 1);
        int cycle = 0;
        for (size_t stage = 0; stage < chain.stages.size(); ++stage) {
            cycle += 1 + m->flops[chain.stages[stage]].extraStages;
            stageCycle[stage] = cycle;
        }
        auto validCombAlias = [&](NodeId node) {
            if (!m->flopAt(node)) {
                if (m->constDrivers.contains(node)) return false;
                if (node < m->in.size() && !m->in[node].empty() && m->in[node].size() != 1) return false;
            }
            return true;
        };
        auto recordBit = [&](NodeId output, int depth, const std::string& src) {
            const std::string port = stripBit(m->nameOf(output));
            observedStages[port][output].insert(depth);
            sourceNames.try_emplace({port, output}, depth, src);
        };
        for (size_t stage = 0; stage < chain.stages.size(); ++stage) {
            const Flop& stageFlop = m->flops[chain.stages[stage]];
            const NodeId q = stageFlop.q;
            std::vector<NodeId> aliases;
            aliasForward(q, aliases);
            for (NodeId node : aliases)
                if (node < m->topOutput.size() && m->topOutput[node] && validCombAlias(node))
                    recordBit(node, stageCycle[stage], flopName(stageFlop));
            for (NodeId source : aliases) {
                if (!validCombAlias(source)) continue;
                for (auto ei : m->out[source]) {
                    const auto& edge = m->edges[ei];
                    if (edge.type != EdgeType::Seq || !edge.exact || edge.ctrl || edge.approximate ||
                        m->isDeadEdge(edge) || m->isConstantProc(edge.proc)) continue;
                    if (m->multiSequential.contains(edge.to) || m->constDrivers.contains(edge.to)) continue;
                    bool singleUnconditionalSource = true;
                    size_t sequentialInputs = 0;
                    for (auto incomingIndex : m->in[edge.to]) {
                        const auto& incoming = m->edges[incomingIndex];
                        if (incoming.type != EdgeType::Seq || m->isDeadEdge(incoming)) continue;
                        ++sequentialInputs;
                        if (incoming.from != source || !incoming.exact || incoming.ctrl || incoming.approximate)
                            singleUnconditionalSource = false;
                    }
                    if (!singleUnconditionalSource || sequentialInputs != 1) continue;
                    auto target = m->flopOfQ.find(edge.to);
                    if (target == m->flopOfQ.end()) continue;
                    const auto& capture = m->flops[target->second];
                    if (!capture.clockResolved || relation(capture.domain, m->flops[chain.stages.front()].domain) != Relation::Same ||
                        !capture.otherDomains.empty() || capture.blocking || capture.edge == 3 || capture.hasIff ||
                        !capture.resets.empty() || capture.syncReset != kNoNode || !capture.syncResetFacts.empty() ||
                        capture.hasSyncResetCandidate || capture.syncDataSuppressed)
                        continue;
                    std::vector<NodeId> capturedAliases;
                    aliasForward(capture.q, capturedAliases);
                    for (NodeId output : capturedAliases)
                        if (output < m->topOutput.size() && m->topOutput[output] && validCombAlias(output))
                            recordBit(output, stageCycle[stage] + 1, flopName(stageFlop));
                }
            }
        }
        for (const auto& [port, bits] : observedStages) {
            std::set<int> distinctCycles;
            size_t distinctBits = 0;
            for (const auto& [bit, cycles] : bits) {
                (void)bit;
                if (cycles.size() != 1) continue;
                ++distinctBits;
                distinctCycles.insert(*cycles.begin());
            }
            if (distinctBits >= 2 && distinctCycles.size() >= 2) {
                if (mappingCount) *mappingCount = distinctBits;
                if (evidence) {
                    for (const auto& [outputBit, cycles] : bits) if (cycles.size() == 1) {
                        if (evidence->size() >= 16) continue;
                        evidence->push_back(m->nameOf(outputBit) + " <= " + sourceNames.at({port, outputBit}).second);
                    }
                }
                return true;
            }
        }
        return false;
    }

    // Feedback is relevant only to this capture's D cone. Stop at other
    // flops, so feedback belonging to a source-domain toggle stays there.
    // Stamps for the feedback searches below: a node is seen in the current search when its stamp equals the search's generation.
    mutable std::vector<uint32_t> backStamp_, foreStamp_, fanoutStamp_;
    mutable uint32_t feedbackGen_ = 0;
    uint32_t nextFeedbackGeneration() const {
        if (backStamp_.size() != m->nodes.size()) {
            backStamp_.assign(m->nodes.size(), 0); foreStamp_.assign(m->nodes.size(), 0); fanoutStamp_.assign(m->nodes.size(), 0);
            feedbackGen_ = 0;
        }
        if (++feedbackGen_ == 0) {
            std::fill(backStamp_.begin(), backStamp_.end(), 0);
            std::fill(foreStamp_.begin(), foreStamp_.end(), 0);
            std::fill(fanoutStamp_.begin(), fanoutStamp_.end(), 0);
            feedbackGen_ = 1;
        }
        return feedbackGen_;
    }

    /// Whether the register's data input depends on its own output through combinational data edges (not through another register).
    /// The path q -> (combinational, non-register nodes) -> D of q is searched from both ends at once, one node each in turn: either
    /// search finding the other end proves it, and either running out disproves it, so the cost is twice the smaller of the two cones.
    // Both feedback answers depend only on the register; chains ask about the same candidate stages again and again.
    mutable std::vector<int8_t> feedbackOf_, proceduralFeedbackOf_;
    // The index of a register held in Model::flops; a copy has none and is not memoised.
    std::optional<size_t> flopIndexOf(const Flop& flop) const {
        if (m->flops.empty() || &flop < m->flops.data() || &flop >= m->flops.data() + m->flops.size()) return std::nullopt;
        return static_cast<size_t>(&flop - m->flops.data());
    }
    bool hasFeedback(const Flop& flop) const {
        const auto index = flopIndexOf(flop);
        if (!index) return searchFeedback(flop);
        if (feedbackOf_.size() != m->flops.size()) feedbackOf_.assign(m->flops.size(), -1);
        int8_t& known = feedbackOf_[*index];
        if (known < 0) known = searchFeedback(flop) ? 1 : 0;
        return known != 0;
    }
    bool proceduralFeedback(const Flop& flop) const {
        const auto index = flopIndexOf(flop);
        if (!index) return searchProceduralFeedback(flop);
        if (proceduralFeedbackOf_.size() != m->flops.size()) proceduralFeedbackOf_.assign(m->flops.size(), -1);
        int8_t& known = proceduralFeedbackOf_[*index];
        if (known < 0) known = searchProceduralFeedback(flop) ? 1 : 0;
        return known != 0;
    }
    bool searchFeedback(const Flop& flop) const {
        if (m->selfLogic.count(flop.q) || m->selfExact.count(flop.q)) return true;
        const NodeId q = flop.q;
        // A loop through other nodes puts them in q's strongly connected component of the data edges: with q alone there,
        // only a data edge from q to itself closes one.
        if (!feedbackGraph_)
            feedbackGraph_ = condense(*m, [](const Edge& e) { return (e.type == EdgeType::Comb || e.type == EdgeType::Seq) && !e.ctrl; });
        if (const uint32_t c = feedbackGraph_->componentOf[q];
            feedbackGraph_->componentEnd[c] - (c ? feedbackGraph_->componentEnd[c - 1] : 0) == 1 &&
            std::none_of(m->in[q].begin(), m->in[q].end(), [&](uint32_t ei) {
                const Edge& e = m->edges[ei];
                return e.type == EdgeType::Seq && !e.ctrl && e.from == q;
            }))
            return false;
        const uint32_t generation = nextFeedbackGeneration();
        // Backward: from the data drivers of q, through combinational data edges, stopping at registers.
        std::vector<NodeId> back;
        for (auto ei : m->in[q]) {
            const Edge& e = m->edges[ei];
            if (e.type == EdgeType::Seq && !e.ctrl) back.push_back(e.from);
        }
        // Forward: from q, through combinational data edges into non-register nodes; a node with a data edge into q closes the loop.
        std::vector<NodeId> fore{q};
        foreStamp_[q] = generation;
        const auto closesLoop = [&](NodeId node) {
            for (auto ei : m->out[node]) {
                const Edge& e = m->edges[ei];
                if (e.type == EdgeType::Seq && !e.ctrl && e.to == q) return true;
            }
            return false;
        };
        while (!back.empty() && !fore.empty()) {
            {   // one backward step
                const NodeId node = back.back();
                back.pop_back();
                if (node == q) return true;
                if (backStamp_[node] != generation && !cones_->isRegister(node)) {
                    backStamp_[node] = generation;
                    for (auto ei : m->in[node]) {
                        const Edge& e = m->edges[ei];
                        if (e.type == EdgeType::Comb && !e.ctrl) back.push_back(e.from);
                    }
                } else backStamp_[node] = generation;
            }
            if (fore.empty()) break;
            {   // one forward step
                const NodeId node = fore.back();
                fore.pop_back();
                if (closesLoop(node)) return true;
                for (auto ei : m->out[node]) {
                    const Edge& e = m->edges[ei];
                    if (e.type != EdgeType::Comb || e.ctrl || foreStamp_[e.to] == generation) continue;
                    foreStamp_[e.to] = generation;
                    if (!cones_->isRegister(e.to)) fore.push_back(e.to);
                }
            }
        }
        return false;
    }

    // Preserve the existing first-stage fanout diagnostic when feedback is
    // routed through procedural combinational logic, while withholding the
    // chain from the recognised synchroniser inventory.
    bool searchProceduralFeedback(const Flop& flop) const {
        std::vector<std::pair<NodeId, bool>> pending;
        for (auto ei : m->in[flop.q]) {
            const auto& e = m->edges[ei];
            if (e.type == EdgeType::Seq && !e.ctrl) pending.emplace_back(e.from, false);
        }
        // Seen with procedural false / true: the two stamp vectors of the feedback search.
        const uint32_t generation = nextFeedbackGeneration();
        // Every path from q back to its data input runs through q's combinational fan-out. When that is small (a register that
        // holds through a mux usually feeds little, and reads much), the backward search keeps to it: nothing outside can lead to q.
        constexpr size_t kFanoutLimit = 4096;
        bool withinFanout = true;
        {
            std::vector<NodeId> work{flop.q};
            size_t reached = 0;
            while (!work.empty() && withinFanout) {
                const NodeId node = work.back();
                work.pop_back();
                for (auto ei : m->out[node]) {
                    const auto& e = m->edges[ei];
                    if (e.type != EdgeType::Comb || e.ctrl || fanoutStamp_[e.to] == generation || cones_->isRegister(e.to)) continue;
                    fanoutStamp_[e.to] = generation;
                    if (++reached > kFanoutLimit) { withinFanout = false; break; }
                    work.push_back(e.to);
                }
            }
        }
        // Otherwise a loop back to q runs inside q's strongly connected component of the data edges, so the search keeps to it.
        if (!withinFanout && !feedbackGraph_)
            feedbackGraph_ = condense(*m, [](const Edge& e) { return (e.type == EdgeType::Comb || e.type == EdgeType::Seq) && !e.ctrl; });
        const uint32_t loopComponent = withinFanout ? 0 : feedbackGraph_->componentOf[flop.q];
        while (!pending.empty()) {
            auto [node, procedural] = pending.back(); pending.pop_back();
            if (node == flop.q) { if (procedural) return true; else continue; }
            if (withinFanout ? fanoutStamp_[node] != generation : feedbackGraph_->componentOf[node] != loopComponent) continue;
            auto& seen = procedural ? foreStamp_ : backStamp_;
            if (seen[node] == generation || cones_->isRegister(node)) continue;
            seen[node] = generation;
            for (auto ei : m->in[node]) {
                const auto& e = m->edges[ei];
                if (e.type == EdgeType::Comb && !e.ctrl)
                    pending.emplace_back(e.from, procedural || m->procs[e.proc].kind == ProcKind::Comb);
            }
        }
        return false;
    }

    mutable std::optional<Condensation> feedbackGraph_;   // components of the data edges, for searchProceduralFeedback
    int minStages_ = 2;
    mutable bool adjacentConsumerIndexReady_ = false;
    mutable std::vector<NodeId> adjacentConsumerFor_;
    // Bound the combined exact adjacent-stage search across all chains. Exhaustion
    // cannot establish a negative, so the affected chain carries an explicit
    // unknown result and callers disclose incomplete CDC-044 coverage.
    mutable size_t adjacentSearchWorkRemaining_ = 1000000;
    /// The stage of `ch` that reaches the required depth: the synchroniser's output. A shorter chain has none, so its last
    /// stage stands in; `full` says which. Stages after it are followers in the destination domain.
    size_t outputStage(const Chain& ch, bool* full = nullptr) const {
        int reached = 0;
        for (size_t stage = 0; stage < ch.stages.size(); ++stage) {
            reached += 1 + m->flops[ch.stages[stage]].extraStages;
            if (reached >= minStages_) { if (full) *full = true; return stage; }
        }
        if (full) *full = false;
        return ch.stages.size() - 1;
    }
    Chain chainFor(uint32_t flopIdx) const {
        Chain ch;
        ch.depth = 1 + m->flops[flopIdx].extraStages;
        ch.stages.push_back(flopIdx);
        const Flop& firstStage = m->flops[flopIdx];
        ch.dualEdgeStage = firstStage.edge == 3;
        if (firstStage.syncDataSuppressed) return ch;
        // The cone stops at registers, but includes any calls that may
        // change the stage input through combinational intermediates.
        ch.callEffectsUnknown = cones_->hasUncertainCalls(flopIdx);
        const bool feedback = hasFeedback(firstStage);
        const bool feedbackInComb = feedback && proceduralFeedback(firstStage);
        if (feedback && !feedbackInComb) return ch;
        // If all non-control data writes to the first capture flop are dead overwrites,
        // do not treat it as an active synchronizer stage.
        {
            bool sawSeqData = false, sawLiveSeqData = false;
            for (auto ei : m->in[firstStage.q]) {
                const Edge& e = m->edges[ei];
                if (e.type != EdgeType::Seq) continue;
                if (e.ctrl && !trustedSyncReset(firstStage, e.from)) return ch;
                if (!e.ctrl) {
                    sawSeqData = true;
                    if (!m->isDeadEdge(e)) sawLiveSeqData = true;
                }
            }
            if (sawSeqData && !sawLiveSeqData) return ch;
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
            bool directHardConsumer = false;
            std::vector<NodeId> logicTargets;
            std::map<NodeId, std::vector<const Edge*>> seqTargets;
            for (auto n : closure) {
                if (n < m->topOutput.size() && m->topOutput[n]) { logicConsumers++; directHardConsumer = true; }   // observable at a top-level port
                for (auto& dm : m->domains) if (dm.root == n) { logicConsumers++; directHardConsumer = true; }     // used as a clock
                for (auto ei : m->out[n]) {
                    auto& e = m->edges[ei];
                    if (m->isDeadEdge(e) || m->isConstantProc(e.proc)) continue;
                    if (e.type == EdgeType::AsyncCtl) { logicConsumers++; directHardConsumer = true; }            // used as an asynchronous reset/set
                    else if (e.type == EdgeType::Comb && !e.exact) { logicConsumers++; logicTargets.push_back(e.to); }
                    else if (e.type == EdgeType::Seq) seqTargets[e.to].push_back(&e);
                }
            }
            // candidate next stage: a flop in the same domain whose only data source is this stage's output
            const Flop* next = nullptr;
            uint32_t nextIdx = 0;
            bool nextLogic = false;
            bool nextMultiInputLogic = false;
            bool nextCallUnknown = false;
            int otherConsumers = logicConsumers;
            for (auto& [tq, es] : seqTargets) {
                auto it = m->flopOfQ.find(tq);
                if (it == m->flopOfQ.end()) { otherConsumers++; continue; }
                const Flop& t = m->flops[it->second];
                const bool targetCallUnknown = cones_->hasUncertainCalls(it->second);
                bool sole = true;
                for (auto ei : m->in[tq]) {
                    auto& e = m->edges[ei];
                    if (e.type != EdgeType::Seq || m->isDeadEdge(e)) continue;
                    if (trustedSyncReset(t, e.from)) continue;
                    if (!cl.count(e.from)) sole = false;
                }
                // a register this stage only enables (`if (s2) held <= 1`) does not carry the value: it is not a stage
                sole = sole && std::any_of(es.begin(), es.end(), [](const Edge* e) { return !e->ctrl; });
                bool sameDomain = t.clockResolved && f.clockResolved && relation(t.domain, f.domain) == Relation::Same && !t.blocking && !f.blocking;
                const bool logic = std::any_of(es.begin(), es.end(), [](const Edge* e) { return !e->exact; });
                // Prefer exact continuations, then prefer a known writer among equivalent candidates.
                // An opaque consumer that is not the next stage remains a tap.
                if (sole && sameDomain && !visited.count(it->second) &&
                    (!next || (nextLogic && !logic) ||
                     (nextLogic == logic && nextCallUnknown && !targetCallUnknown))) {
                    if (next) otherConsumers++;
                    next = &t; nextIdx = it->second; nextLogic = logic; nextCallUnknown = targetCallUnknown;
                } else otherConsumers++;
            }
            // Before depth 2, a single non-feedback, non-enabled same-domain register fed from stage 1 through
            // multi-input logic (either directly in its sequential expression or via a combinational intermediate)
            // is also broken inter-stage logic (CDC-006).
            if (!next && first && ch.depth < 2 && !feedbackInComb && !directHardConsumer) {
                auto isCleanStage2LogicTarget = [&](uint32_t tIdx) {
                    const Flop& t = m->flops[tIdx];
                    if (!t.clockResolved || !f.clockResolved ||
                        relation(t.domain, f.domain) != Relation::Same ||
                        t.blocking || f.blocking || visited.count(tIdx) || hasFeedback(t))
                        return false;
                    for (auto ei : m->in[t.q]) {
                        const Edge& e = m->edges[ei];
                        if (e.type == EdgeType::Seq && !m->isDeadEdge(e) && e.ctrl && !trustedSyncReset(t, e.from))
                            return false;
                    }
                    return true;
                };
                if (logicTargets.empty() && seqTargets.size() == 1) {
                    const auto& [tq, es] = *seqTargets.begin();
                    auto it = m->flopOfQ.find(tq);
                    if (it != m->flopOfQ.end() && isCleanStage2LogicTarget(it->second) &&
                        std::any_of(es.begin(), es.end(), [](const Edge* e) { return !e->ctrl && !e->exact; })) {
                        const Flop& t = m->flops[it->second];
                        next = &t; nextIdx = it->second; nextLogic = true; nextMultiInputLogic = true;
                        nextCallUnknown = cones_->hasUncertainCalls(it->second);
                        otherConsumers = 0;
                    }
                } else if (seqTargets.empty() && !logicTargets.empty()) {
                    std::vector<NodeId> work = logicTargets;
                    std::unordered_set<NodeId> seenComb;
                    std::set<uint32_t> combSeqTargets;
                    bool valid = true;
                    while (!work.empty() && valid) {
                        const NodeId node = work.back();
                        work.pop_back();
                        if (!seenComb.insert(node).second) continue;
                        if (seenComb.size() > 64 || node >= m->nodes.size() || m->flopOfQ.count(node)) {
                            valid = false; break;
                        }
                        if (node < m->topOutput.size() && m->topOutput[node]) { valid = false; break; }
                        for (const auto& dm : m->domains)
                            if (dm.root == node) { valid = false; break; }
                        if (!valid) break;
                        for (auto ei : m->out[node]) {
                            const auto& e = m->edges[ei];
                            if (m->isDeadEdge(e) || m->isConstantProc(e.proc)) continue;
                            if (e.approximate || e.type == EdgeType::AsyncCtl) { valid = false; break; }
                            if (e.type == EdgeType::Comb) work.push_back(e.to);
                            else if (e.type == EdgeType::Seq) {
                                if (e.ctrl) { valid = false; break; }
                                auto it = m->flopOfQ.find(e.to);
                                if (it == m->flopOfQ.end()) { valid = false; break; }
                                combSeqTargets.insert(it->second);
                            }
                        }
                    }
                    if (valid && combSeqTargets.size() == 1) {
                        const uint32_t tIdx = *combSeqTargets.begin();
                        if (isCleanStage2LogicTarget(tIdx)) {
                            const Flop& t = m->flops[tIdx];
                            next = &t; nextIdx = tIdx; nextLogic = true; nextMultiInputLogic = true;
                            nextCallUnknown = cones_->hasUncertainCalls(tIdx);
                            otherConsumers = 0;
                        }
                    }
                }
            }
            // Past two stages the chain is complete, and a register fed through logic is its consumer rather than a broken stage.
            if (next && nextLogic && ch.depth >= 2) break;
            if (nextCallUnknown && ch.depth < minStages_) ch.callEffectsUnknown = true;
            if (next && !first) {
                // logic that combines this stage with the next one (stage XOR delayed copy) is an edge detector, not a tap
                // An edge detector compares the two copies as data: XOR/XNOR (or != / ==), or AND with exactly one of them inverted
                // (`s3 & ~s2`). OR, plain AND or a guard on them is not one.
                std::vector<NodeId> nextClosure;
                aliasForward(next->q, nextClosure);
                std::unordered_set<NodeId> nextCl(nextClosure.begin(), nextClosure.end());
                auto detects = [&](NodeId target, EdgeType type, char& idleValue) {
                    const Edge *mine = nullptr, *theirs = nullptr;
                    bool extraData = false;
                    unsigned mineCount = 0, theirsCount = 0;
                    for (auto ei : m->in[target]) {
                        const Edge& e = m->edges[ei];
                        if (e.type != type || m->isDeadEdge(e) || m->isConstantProc(e.proc)) continue;
                        if (cl.count(e.from)) { mine = &e; mineCount++; }
                        else if (nextCl.count(e.from)) { theirs = &e; theirsCount++; }
                        else if (!e.ctrl) extraData = true;
                    }
                    if (!mine || !theirs || mine->ctrl != theirs->ctrl) return false;
                    if (type == EdgeType::Seq) {
                        const Flop* detFlop = m->flopAt(target);
                        if (!detFlop || !detFlop->clockResolved || detFlop->blocking || detFlop->hasIff ||
                            !detFlop->otherDomains.empty() || m->multiSequential.count(target) ||
                            relation(detFlop->domain, f.domain) != Relation::Same ||
                            detFlop->edge == 3 || detFlop->edge != f.edge)
                            return false;
                        return hasExactStageComparison(*m, target, mine->from, theirs->from, &idleValue) ||
                               hasExactStageXor(*m, target, mine->from, theirs->from, &idleValue) ||
                               hasExactStageInvertedAnd(*m, target, mine->from, theirs->from, &idleValue);
                    }
                    (void)extraData;
                    if (mine->proc != theirs->proc) return false;
                    return hasExactCombinationalComparison(*m, *mine, *theirs, target, mine->from, theirs->from, &idleValue) ||
                           hasExactCombinationalXor(*m, *mine, *theirs, target, mine->from, theirs->from, &idleValue) ||
                           hasExactCombinationalInvertedAnd(*m, *mine, *theirs, target, mine->from, theirs->from, &idleValue);
                };
                bool detectorAtBoundary = false;
                std::map<NodeId, char> detectorTargets;
                for (auto lt : logicTargets) {
                    char idle = '0';
                    const bool exactDetector = detects(lt, EdgeType::Comb, idle);
                    if (exactDetector) detectorTargets.emplace(lt, idle);
                    detectorAtBoundary = exactDetector || detectorAtBoundary;
                }
                // ... also when the comparison is written in a clocked block (`pulse <= s2 ^ s3`)
                for (auto& [tq, es] : seqTargets) if (tq != next->q) {
                    char idle = '0';
                    const bool exactDetector = detects(tq, EdgeType::Seq, idle);
                    if (exactDetector) detectorTargets.emplace(tq, idle);
                    detectorAtBoundary = exactDetector || detectorAtBoundary;
                }
                ch.edgeDetector = ch.edgeDetector || detectorAtBoundary;
                for (const auto& [target, idle] : detectorTargets)
                    ch.detectorSites.push_back({ch.stages.size() - 1, ch.depth, target, idle});
                // A follower is outside the synchronizer only when it terminates
                // the chain and every observable use is part of this exact detector.
                // Mixed followers (detector plus another stage/data use) stay in it.
                bool validDetectorTargets = !f.hasIff && !next->hasIff;
                for (const auto& [target, idle] : detectorTargets) {
                    if (const Flop* df = m->flopAt(target)) {
                        for (size_t ri = 0; ri < df->resets.size(); ++ri)
                            if (ri >= df->resetValues.size() || df->resetValues[ri] != idle) validDetectorTargets = false;
                        for (const auto& fact : df->syncResetFacts)
                            if (fact.forcedValue != idle) validDetectorTargets = false;
                    }
                }
                if (detectorAtBoundary && validDetectorTargets && ch.depth >= minStages_ && ch.edgeDetectorBoundary == 0 && !detectorTargets.empty()) {
                    std::vector<NodeId> followerAliases;
                    aliasForward(next->q, followerAliases);
                    std::vector<NodeId> pending(followerAliases.begin(), followerAliases.end());
                    std::unordered_set<NodeId> seen;
                    bool onlyDetector = true;
                    bool sawDetector = false;
                    while (!pending.empty() && onlyDetector) {
                        const NodeId source = pending.back(); pending.pop_back();
                        if (!seen.insert(source).second) continue;
                        // The detector result may be internal and then feed
                        // arbitrary downstream logic (for example a counter).
                        // Once its exact node is reached, later consumers belong
                        // to the detector output, not to the follower register.
                        if (detectorTargets.count(source)) {
                            sawDetector = true;
                            continue;
                        }
                        if (source < m->topOutput.size() && m->topOutput[source]) {
                            onlyDetector = false;
                        }
                        for (auto ei : m->out[source]) {
                            const auto& edge = m->edges[ei];
                            if (edge.approximate) onlyDetector = false;
                            else if (edge.type == EdgeType::Comb) pending.push_back(edge.to);
                            else if (edge.type == EdgeType::Seq) {
                                if (!detectorTargets.count(edge.to)) onlyDetector = false;
                                else sawDetector = true;
                            } else if (edge.type == EdgeType::AsyncCtl) onlyDetector = false;
                        }
                    }
                    for (const auto& domain : m->domains)
                        if (seen.count(domain.root) && !detectorTargets.count(domain.root))
                            onlyDetector = false;
                    // The next loop iteration appends this follower. A later
                    // synchronizer stage therefore prevents exclusion here.
                    if (onlyDetector && sawDetector) ch.edgeDetectorBoundary = ch.stages.size();
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
            if (otherConsumers > 0 && ch.depth < minStages_) {
                for (auto target : logicTargets) {
                    auto name = stripBit(m->nameOf(target));
                    if (!name.empty() && name != "<unknown>") {
                        SourceLocation loc;
                        for (auto ei : m->in[target]) if (m->edges[ei].type == EdgeType::Comb) { loc = m->procs[m->edges[ei].proc].loc; break; }
                        ch.fanoutTargets.push_back({name, loc});
                    }
                }
                for (const auto& [target, edges] : seqTargets) {
                    if (next && target == next->q) continue;
                    auto name = stripBit(m->nameOf(target));
                    if (!name.empty() && name != "<unknown>") {
                        SourceLocation loc;
                        auto flop = m->flopOfQ.find(target);
                        if (flop != m->flopOfQ.end()) loc = procOf(m->flops[flop->second]).loc;
                        else if (!edges.empty()) loc = m->procs[edges.front()->proc].loc;
                        ch.fanoutTargets.push_back({name, loc});
                    }
                }
                for (auto alias : closure)
                    if (alias < m->topOutput.size() && m->topOutput[alias]) {
                        SourceLocation loc;
                        for (auto ei : m->in[alias])
                            if (m->edges[ei].type == EdgeType::Comb && cl.count(m->edges[ei].from)) { loc = m->procs[m->edges[ei].proc].loc; break; }
                        ch.fanoutTargets.push_back({stripBit(m->nameOf(alias)), loc});
                    }
                std::sort(ch.fanoutTargets.begin(), ch.fanoutTargets.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
                ch.fanoutTargets.erase(std::unique(ch.fanoutTargets.begin(), ch.fanoutTargets.end(), [](const auto& a, const auto& b) { return a.name == b.name; }), ch.fanoutTargets.end());
            }
            if (next) {
                for (auto& [tq, es] : seqTargets) {
                    auto it = m->flopOfQ.find(tq);
                    if (it != m->flopOfQ.end() && m->flops[it->second].clockResolved && f.clockResolved &&
                        relation(m->flops[it->second].domain, f.domain) != Relation::Same && ch.depth < minStages_) ch.leaksDomain = true;
                }
                if (ch.depth < minStages_) {   // past the required depth, the next register is not part of the synchroniser
                    if (next->edge == 3) ch.dualEdgeStage = true;
                    if (next->edge != f.edge && next->edge != 3 && f.edge != 3) ch.clockMismatch = true;
                    if (asyncResetSignature(*next) != asyncResetSignature(f)) ch.resetMismatch = true;
                }
            }
            if (!next) break;
            if (nextLogic) { ch.linkLogic = true; ch.multiInputLinkLogic = nextMultiInputLogic; ch.linkTarget = next->q; break; }   // logic between any two stages
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
        if (!firstStage.syncResetGuards.empty()) {
            for (size_t i = 1; i < ch.stages.size(); ++i) {
                const NodeId laterQ = m->flops[ch.stages[i]].q;
                if (std::any_of(firstStage.syncResetGuards.begin(), firstStage.syncResetGuards.end(), [&](NodeId g) {
                        return g == laterQ || m->aliasRoot(g) == laterQ;
                    })) {
                    ch.depth = 1;
                    ch.stages.resize(1);
                    break;
                }
            }
        }
        return ch;
    }

    // A syntactic reset candidate is not enough to make a synchronizer stage
    // clean. A register guard must be launched in the stage's destination
    // domain; a primary-input reset remains an explicit reset obligation.
    bool trustedSyncReset(const Flop& f, NodeId guard) const {
        if (guard == kNoNode ||
            std::find(f.syncResetGuards.begin(), f.syncResetGuards.end(), guard) == f.syncResetGuards.end()) return false;
        std::vector<NodeId> work{guard};
        std::set<NodeId> seen;
        size_t visits = 0;
        while (!work.empty()) {
            const NodeId node = work.back(); work.pop_back();
            if (!seen.insert(node).second) continue;
            if (++visits > 64 || node >= m->nodes.size() ||
                (node < m->blackBoxDriven.size() && m->blackBoxDriven[node]) ||
                (node < m->unresolvedDriven.size() && m->unresolvedDriven[node])) return false;
            bool clock = false;
            for (const auto& domain : m->domains)
                if (domain.root == node || (domain.root != kNoNode && m->aliasRoot(node) == domain.root)) clock = true;
            if (clock) return false;
            if (node < m->topInput.size() && m->topInput[node]) continue;
            if (const Flop* source = m->flopAt(node)) {
                if (!source->clockResolved || !f.clockResolved || relation(source->domain, f.domain) != Relation::Same) return false;
                continue;
            }
            if (node >= m->in.size() || m->in[node].empty() || m->constDrivers.contains(node)) return false;
            const auto drivers = m->in[node];
            const auto proc = m->edges[drivers[0]].proc;
            for (auto ei : drivers) {
                const Edge& edge = m->edges[ei];
                // A resolved net with independent drivers is not one Boolean expression.
                if (edge.proc != proc || edge.type != EdgeType::Comb || edge.ctrl || edge.approximate ||
                    !(edge.exact || edge.flip || edge.op == Op::And || edge.op == Op::Or || edge.op == Op::Not)) return false;
                work.push_back(edge.from);
            }
        }
        return !seen.empty();
    }

    void findAdjacentStageUses(Chain& ch) const {
        if (ch.adjacentStageSearchDone) return;
        ch.adjacentStageSearchDone = true;
        // Collect data and control influence after the complete chain is known.
        // Propagate each stage only through combinational graph edges, stopping
        // at registers. A scalar output or capture is evidence when any adjacent
        // pair contributes, even if other stages also reach the same consumer.
        ch.adjacentStageUse = false;
        ch.adjacentStageUses.clear();
        ch.adjacentStageUseCount = 0;
        ch.adjacentStageSearchIncomplete = false;
        // A single-stage capture cannot contain an adjacent-stage witness.
        // Skip its cone walk entirely; this is common in wide fanout designs.
        if (ch.linkLogic || ch.feedbackWithFanout || ch.depth < 2 || ch.stages.size() < 2)
            return;
        prepareAdjacentConsumers();
        std::map<NodeId, std::set<size_t>> origins;
        std::vector<std::pair<NodeId, size_t>> pending;
        auto adjacentPair = [](const std::set<size_t>& stages) -> std::optional<std::pair<size_t, size_t>> {
            for (auto it = stages.begin(); it != stages.end(); ++it) {
                auto next = std::next(it);
                if (next != stages.end() && *next - *it == 1) return std::pair{*it, *next};
            }
            return std::nullopt;
        };
        for (size_t stage = 0; stage < ch.stages.size(); ++stage) {
            std::vector<NodeId> aliases;
            aliasForward(m->flops[ch.stages[stage]].q, aliases);
            for (NodeId node : aliases) pending.emplace_back(node, stage);
        }
        for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
            if (adjacentSearchWorkRemaining_ == 0) {
                ch.adjacentStageSearchIncomplete = true;
                break;
            }
            --adjacentSearchWorkRemaining_;
            const auto [node, stage] = pending[cursor];
            if (!origins[node].insert(stage).second) continue;
            if (node >= m->out.size()) continue;
            const auto pair = adjacentPair(origins[node]);
            if (pair && node < adjacentConsumerFor_.size() && adjacentConsumerFor_[node] != kNoNode) {
                ch.adjacentStageUse = true;
                ch.adjacentStageUseCount = 1;
                ch.adjacentStageUses.emplace_back(pair->first, pair->second, adjacentConsumerFor_[node]);
                // The positive witness is sufficient; no total was measured.
                return;
            }
            for (auto ei : m->out[node]) {
                const auto& edge = m->edges[ei];
                if (edge.type == EdgeType::Comb && !edge.approximate && !m->flopOfQ.count(edge.to) &&
                    !m->isDeadEdge(edge) && !m->isConstantProc(edge.proc) &&
                    edge.to < adjacentConsumerFor_.size() && adjacentConsumerFor_[edge.to] != kNoNode)
                    pending.emplace_back(edge.to, stage);
            }
        }
        auto recordConsumer = [&](NodeId target, const std::set<size_t>& stages) {
            const auto pair = adjacentPair(stages);
            if (!pair) return;
            ch.adjacentStageUse = true;
            ++ch.adjacentStageUseCount;
            constexpr size_t maxConsumerDetails = 16;
            if (ch.adjacentStageUses.size() < maxConsumerDetails)
                ch.adjacentStageUses.emplace_back(pair->first, pair->second, target);
        };
        // Inspect only reached nodes and their outgoing edges. A per-chain scan
        // of every model node multiplied unrelated sink width by chain count.
        std::map<NodeId, std::set<size_t>> sequentialConsumers;
        for (const auto& [node, stages] : origins) {
            if (node < m->topOutput.size() && m->topOutput[node]) recordConsumer(node, stages);
            if (node >= m->out.size()) continue;
            for (auto ei : m->out[node]) {
                const auto& edge = m->edges[ei];
                if (edge.type == EdgeType::Seq && !edge.approximate && !m->isDeadEdge(edge) && !m->isConstantProc(edge.proc)) {
                    if (const Flop* dstFlop = m->flopAt(edge.to); dstFlop && dstFlop->blocking) continue;
                    sequentialConsumers[edge.to].insert(stages.begin(), stages.end());
                }
            }
        }
        for (const auto& [target, stages] : sequentialConsumers) recordConsumer(target, stages);
        std::sort(ch.adjacentStageUses.begin(), ch.adjacentStageUses.end());
        ch.adjacentStageUses.erase(std::unique(ch.adjacentStageUses.begin(), ch.adjacentStageUses.end()),
                                   ch.adjacentStageUses.end());
        return;
    }

    using Cone = model::Cone;
    model::ConeFinder* cones_ = nullptr;
    std::shared_ptr<const Cone> coneOf(const Flop& d) { return cones_->of(d); }

    struct Group {
        std::string srcName, dstName;
        uint32_t srcDomain = 0, dstDomain = 0;
        std::vector<uint32_t> dstFlops;
        std::set<NodeId> srcBits;
        std::vector<std::pair<NodeId, NodeId>> sourceCaptures;   // sorted, unique (a set of up to billions of pairs costs 48 bytes each)
        bool anyLogicBefore = false;
        bool collapsed = false;       // tracked at whole-symbol granularity: no per-bit classification possible
        uint64_t realWidth = 0;
        bool gray = false, counter = false, qualified = false, memory = false;
        bool unresolvedLanes = false;
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
    std::set<std::pair<std::string, std::string>> unresolvedQualificationPairs_; // (srcName, dstName) with Undecided CDC-039 emitted
    std::map<std::pair<std::string, std::string>, std::pair<NodeId, NodeId>> halfCycle_;   // rising<->falling-edge register paths
    void analyseStructure();   // cdc_protocol.cpp: the structure analyses below
    void reconvergence();      // CDC-010 / 011 / 036
    void clockStructure();     // CDC-019 / 020 / 030 / 031
    void edgeStructure();      // CDC-043
    /// The reset_contracts binding of this design (rdc::bindContracts, pure); computed once per run by this family on first use.
    const model::ContractBindingResult& contractBinding();
    /// Cites the release orders between two sets of reset roots as evidence of one RDC-023/024 finding; never changes its severity.
    void citeReleaseOrders(Diagnostic& d, const std::set<NodeId>& a, const std::set<NodeId>& b);
    void protocols();          // CDC-012 / 013 / 014 / 015 / 024 / 034 / 035 / 037 / 038 / 040 / 041 / 042 / 044
    void sequentialElements(); // CDC-001 latches
    void libraryCells();       // cdc_library.cpp: CDC-020 / 035 / 045 / 046 for declared library RAMs, clock gates and black boxes
    int domainOfClockNode(NodeId clk) const;
    double periodNs(uint32_t domain, bool* unitKnown = nullptr) const;

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
    // Breadth-first search state for pathWitness, indexed by node: the edge towards the capture, valid while the stamp matches.
    mutable std::vector<uint32_t> witnessStamp_, witnessEdge_, witnessSource_;
    mutable uint32_t witnessGen_ = 0, witnessSourceGen_ = 0;
    // Per node, a summary of the nodes the backward search can reach from it (itself included): launch-domain bits (bit 63 for
    // anything that is not a register of a resolved domain below 63) and a 64-bit hash of the registers. Built once.
    mutable std::vector<std::array<uint64_t, 2>> witnessMasks_;
    // The Comb in-edges of every node that is not a register, in m->in order, packed: the search reads nothing else.
    mutable std::vector<uint32_t> witnessInStart_, witnessInFrom_, witnessInEdge_;
    static constexpr size_t kWitnessMaxSteps = 32, kWitnessMaxVisited = 100000, kWitnessMaxGroupVisited = kWitnessMaxVisited;
    static uint64_t witnessRegBit(NodeId n) { return uint64_t{1} << ((static_cast<uint64_t>(n) * 0x9E3779B97F4A7C15ull) >> 58); }
    uint64_t witnessOwnDom(NodeId n) const {
        if (!cones_->isRegister(n)) return uint64_t{1} << 63;
        const Flop& f = m->flops[cones_->flopIndexOf(n)];
        return f.clockResolved && f.domain < 63 ? uint64_t{1} << f.domain : uint64_t{1} << 63;
    }
    // The masks follow the search's edges reversed: a Comb edge p -> n carries p's masks into n unless n is a register (the
    // search never expands a register).
    void witnessBuildMasks() const {
        const size_t n = m->nodes.size();
        std::vector<uint64_t> dom(n, 0), reg(n, 0);
        for (NodeId v = 0; v < n; v++) {
            dom[v] = witnessOwnDom(v);
            if (cones_->isRegister(v)) reg[v] = witnessRegBit(v);
        }
        closeMasks(*m, dom, reg, [&](const Edge& e) { return e.type == EdgeType::Comb && !cones_->isRegister(e.to); });
        witnessMasks_.resize(n);
        for (NodeId v = 0; v < n; v++) witnessMasks_[v] = {dom[v], reg[v]};
        witnessInStart_.assign(n + 1, 0);
        witnessInFrom_.clear(); witnessInEdge_.clear();
        for (NodeId v = 0; v < n; v++) {
            witnessInStart_[v] = static_cast<uint32_t>(witnessInFrom_.size());
            if (cones_->isRegister(v)) continue;
            for (auto ei : m->in[v])
                if (m->edges[ei].type == EdgeType::Comb) { witnessInFrom_.push_back(m->edges[ei].from); witnessInEdge_.push_back(ei); }
        }
        witnessInStart_[n] = static_cast<uint32_t>(witnessInFrom_.size());
    }
    // The path steps from source node `n` back to `capture` along the recorded first edges.
    std::vector<Evidence> witnessSteps(NodeId n, NodeId capture) const {
        std::vector<Evidence> steps;
        for (NodeId cur = n; cur != capture; cur = m->edges[witnessEdge_[cur]].to) {
            const Edge& e = m->edges[witnessEdge_[cur]];
            if (steps.size() == kWitnessMaxSteps) { steps.push_back({"path_step", "... continues to " + m->nameOf(capture), {}}); break; }
            steps.push_back({"path_step", m->nameOf(e.from) + " -> " + m->nameOf(e.to), m->procs[e.proc].loc});
        }
        return steps;
    }
    // The shortest path from one launch bit to one capture bit, in signal order; endpoints alone do not show where logic sits.
    //
    // The search is backward from the capture bit through Comb edges (Seq edges only at the capture), registers not expanded;
    // the first popped node that is a source wins. It skips (neither pushes nor counts) every node whose masks share no bit
    // with the sources' own bits: no source is reachable from such a node. A node from which a source is reachable is only
    // ever first reached from a parent from which that source is reachable too, so those nodes are pushed in the same
    // relative order, by the same first edge, as without the skip: the source and path found are the same, and only the
    // cut-off changes, because skipped positions no longer count towards the limits.
    std::vector<Evidence> pathWitness(const Group& g) const {
        // Evidence only: each capture bit's search is bounded, and so is the group's, so a group of many captures behind a cone
        // too wide to search does not repeat the bounded search for every bit.
        size_t groupVisited = 0;
        ++witnessCalls_;
        if (witnessStamp_.size() != m->nodes.size()) {
            witnessStamp_.assign(m->nodes.size(), 0); witnessEdge_.assign(m->nodes.size(), 0); witnessSource_.assign(m->nodes.size(), 0);
            witnessGen_ = witnessSourceGen_ = 0;
            witnessBuildMasks();
        }
        // The group's source bits, marked once instead of looked up for every node visited.
        if (++witnessSourceGen_ == 0) { std::fill(witnessSource_.begin(), witnessSource_.end(), 0); witnessSourceGen_ = 1; }
        uint64_t sigDom = 0, sigReg = 0;
        for (const NodeId bit : g.srcBits) {
            witnessSource_[bit] = witnessSourceGen_;
            sigDom |= witnessOwnDom(bit);
            sigReg |= cones_->isRegister(bit) ? witnessRegBit(bit) : ~uint64_t{0};
        }
        // A source that is not a register has no register bit, so the register test applies only when every source is one.
        const bool regTest = sigReg != ~uint64_t{0};
        const auto keep = [&](NodeId n) {
            const auto& mask = witnessMasks_[n];
            return (mask[0] & sigDom) && (!regTest || (mask[1] & sigReg));
        };
        std::vector<NodeId> queue;
        for (auto di : g.dstFlops) {
            if (groupVisited >= kWitnessMaxGroupVisited) break;
            const NodeId capture = m->flops[di].q;
            if (++witnessGen_ == 0) { std::fill(witnessStamp_.begin(), witnessStamp_.end(), 0); witnessGen_ = 1; }
            queue.clear();
            for (auto ei : m->in[capture]) {
                const Edge& e = m->edges[ei];
                if (e.type != EdgeType::Seq || !keep(e.from) || witnessStamp_[e.from] == witnessGen_) continue;
                witnessStamp_[e.from] = witnessGen_;
                witnessEdge_[e.from] = ei;
                queue.push_back(e.from);
            }
            for (size_t i = 0; i < queue.size() && i < kWitnessMaxVisited && groupVisited < kWitnessMaxGroupVisited; i++, groupVisited++) {
                const NodeId n = queue[i];
                ++witnessPositions_;
                if (witnessSource_[n] == witnessSourceGen_) { ++witnessFound_; return witnessSteps(n, capture); }
                ++witnessExpanded_;
                // A register ends the combinational cone: it has no packed in-edges.
                witnessEdgeScans_ += witnessInStart_[n + 1] - witnessInStart_[n];
                for (uint32_t k = witnessInStart_[n]; k < witnessInStart_[n + 1]; ++k) {
                    const NodeId from = witnessInFrom_[k];
                    if (witnessStamp_[from] == witnessGen_ || !keep(from)) continue;
                    witnessStamp_[from] = witnessGen_;
                    witnessEdge_[from] = witnessInEdge_[k];
                    queue.push_back(from);
                }
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

    /// Registers that capture a value of uncertain origin, by capture symbol. Filled from the full cones by analyseCrossings, or by
    /// analyseCaptureUncertainty when no crossing rule runs.
    struct CaptureSets {
        std::map<std::string, std::vector<uint32_t>> bb, unresolved, undriven;
        std::map<std::pair<std::string, uint32_t>, std::vector<uint32_t>> calls;
        std::map<std::string, std::set<std::string>> undrivenLeaves;
    };

    // ---- sources that cannot be classified: never silent
    void reportUnresolvedCaptures(CaptureSets& cap) {
        auto& bbCaptures = cap.bb;
        auto& unresolvedCaptures = cap.unresolved;
        auto& undrivenCaptures = cap.undriven;
        auto& callCaptures = cap.calls;
        auto& undrivenLeaves = cap.undrivenLeaves;
        auto unresolvedSource = [&](const char* rule, std::map<std::string, std::vector<uint32_t>>& caps, const char* what, const char* gapScope) {
            for (auto& [sym, dis] : caps) {
                const Flop& f0 = m->flops[dis[0]];
                Diagnostic d = mk(rule, Severity::Warning,
                                  "register '" + sym + "' (" + std::to_string(dis.size()) + " bit(s)) captures a value that " + what +
                                  "; whether it crosses a clock domain is unresolved",
                                  procOf(f0).loc, procOf(f0).instancePath, Confidence::Unresolved);
                d.identityKey = sym + ":" + (&caps == &bbCaptures ? "blackbox" : &caps == &undrivenCaptures ? "undriven" : "truncated");
                d.evidence.push_back({"capture_register", sym, procOf(f0).loc});
                d.evidence.push_back({"site_signal", sym, procOf(f0).loc});
                if (&caps == &undrivenCaptures)
                    for (const auto& leaf : undrivenLeaves[sym]) d.evidence.push_back({"undriven_source", leaf, {}});
                d.nextStep = "Model the source (provide the module, remove the unsupported construct) or review manually.";
                add(std::move(d));
                incomplete(gapScope, sym + ": " + what);
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
            incomplete("graph-precision", reg + ": unresolved call '" + issue.call + "'");
        }
    }

    /// The CST-010 findings that crossings otherwise produce (a capture whose cone holds a truncated, undriven or unresolved-call source),
    /// computed without the cones' sources. Same registers, same symbols, same text as the crossing stage.
    void analyseCaptureUncertainty() {
        CaptureSets cap;
        const auto stageStart = Timings::Clock::now();
        for (const auto& [di, u] : cones_->uncertaintyOfRegisters()) {
            const Flop& d = m->flops[di];
            const std::string dstSym = stripBit(m->nameOf(d.q));
            for (auto issue : u.calls) cap.calls[{dstSym, issue}].push_back(di);
            if (u.blackBox) cap.bb[dstSym].push_back(di);
            if (u.unresolved) cap.unresolved[dstSym].push_back(di);
            if (u.undriven) cap.undriven[dstSym].push_back(di);
        }
        if (cfg->timings) cfg->timings->record("cdc:capture_uncertainty:masks_and_calls", stageStart);
        // The nets are named per symbol, and the symbol's registers are walked together: a net shared by several bits is found once.
        for (const auto& [sym, flops] : cap.undriven)
            for (auto leaf : cones_->undrivenOf(flops)) cap.undrivenLeaves[sym].insert(stripBit(m->nameOf(leaf)));
        reportUnresolvedCaptures(cap);
    }

    void analyseCrossings() {
        const auto crossingStart = Timings::Clock::now();
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
        std::unordered_map<NodeId, std::vector<Chain::FanoutTarget>> topOutputAliases;
        for (NodeId n = 0; n < m->nodes.size(); ++n)
            if (m->topOutput[n]) {
                SourceLocation loc;
                for (auto ei : m->in[n])
                    if (m->edges[ei].type == EdgeType::Comb) { loc = m->procs[m->edges[ei].proc].loc; break; }
                topOutputAliases[aliasRoot(n)].push_back({stripBit(m->nameOf(n)), loc});
            }
        std::map<std::pair<NodeId, uint32_t>, std::vector<uint32_t>> inputsToDomain;   // (input node, dst domain) -> dst flops
        std::set<std::pair<uint32_t, uint32_t>> unknownPairs;
        std::map<std::pair<uint32_t, uint32_t>, uint64_t> unknownPairCount;
        uint64_t unresolvedEnds = 0;
        std::set<std::string> unresolvedClockSignals;
        CaptureSets cap;
        auto& bbCaptures = cap.bb;
        auto& unresolvedCaptures = cap.unresolved;
        auto& undrivenCaptures = cap.undriven;
        auto& callCaptures = cap.calls;
        auto& undrivenLeaves = cap.undrivenLeaves;
        chains_.assign(m->flops.size(), Chain{});
        haveChain_.assign(m->flops.size(), 0);
        auto& chains = chains_;
        auto& haveChain = haveChain_;
        std::vector<char> incompleteSyncSource(m->flops.size());

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
        uint64_t crossingGroups = 0, crossingSourceBits = 0, crossingDestinationLinks = 0;
        uint64_t inputDomainGroups = 0, inputCaptureLinks = 0;
        const auto checkpointCrossings = [&](uint64_t visited) {
            auto counters = cones_->telemetry();
            counters.emplace_back("cdc.crossings.capture_registers_visited", visited);
            counters.emplace_back("cdc.crossings.groups", crossingGroups);
            counters.emplace_back("cdc.crossings.source_bits", crossingSourceBits);
            counters.emplace_back("cdc.crossings.destination_links", crossingDestinationLinks);
            counters.emplace_back("cdc.crossings.input_domain_groups", inputDomainGroups);
            counters.emplace_back("cdc.crossings.input_capture_links", inputCaptureLinks);
            counters.emplace_back("cdc.crossings.source_bit_payload_bytes_estimate", crossingSourceBits * sizeof(NodeId));
            counters.emplace_back("cdc.crossings.destination_link_payload_bytes_estimate", crossingDestinationLinks * sizeof(uint32_t));
            counters.emplace_back("cdc.crossings.input_link_payload_bytes_estimate", inputCaptureLinks * sizeof(uint32_t));
            cfg->timings->checkpoint(counters);
        };

        const auto syncResetGuardTimedSame = [&](const Flop& df, NodeId guard) -> bool {
            if (!trustedSyncReset(df, guard)) return false;
            std::vector<NodeId> work{guard};
            std::unordered_set<NodeId> seen;
            size_t visits = 0;
            while (!work.empty()) {
                const NodeId node = work.back();
                work.pop_back();
                if (!seen.insert(node).second) continue;
                if (++visits > 64 || node >= m->nodes.size()) return false;
                if (node < m->topInput.size() && m->topInput[node]) {
                    bool rejectedMode = false;
                    const auto contracts = rel_->effectiveInputDelays(node, &rejectedMode);
                    if (rejectedMode || contracts.empty()) return false;
                    if (!std::all_of(contracts.begin(), contracts.end(), [&](const auto& active) {
                            return rel_->relationToName(df.domain, active.delay->clock) == Relation::Same;
                        }))
                        return false;
                    continue;
                }
                if (m->flopAt(node)) {
                    if (m->multiSequential.contains(node) || m->collapsed(node)) return false;
                    continue;
                }
                for (auto ei : m->in[node]) {
                    const Edge& edge = m->edges[ei];
                    if (edge.type == EdgeType::Comb) work.push_back(edge.from);
                }
            }
            return true;
        };

        // A source in the capture's own domain, on a compatible edge and with a Same relation, is ignored below. A cone usually holds
        // thousands of those (a bus multiplexer), so the walk summarises them: only the count of distinct sources and the same-name
        // domain pairs (CDC-037) are read from them. See ANALYSIS.md, "How cones are computed once".
        const auto crossingClasses = cones_->classify([&](uint32_t fi) {
            const Flop& f = m->flops[fi];
            if (!f.clockResolved || !f.otherDomains.empty() || m->multiSequential.count(f.q)) return SourceClasses::kListed;
            return f.domain * 4u + static_cast<uint32_t>(f.edge & 3);
        }, true);
        std::unordered_map<uint64_t, bool> ignorable;
        const auto ignoredBy = [&](const Flop& d, uint32_t set) {
            auto [it, fresh] = ignorable.try_emplace(uint64_t{set} << 32 | uint64_t{d.domain} << 2 | static_cast<uint64_t>(d.edge & 3), true);
            if (!fresh) return it->second;
            for (const uint32_t cls : crossingClasses.classes(set)) {
                const uint32_t launch = cls >> 2, edge = cls & 3;
                if (relation(launch, d.domain) != Relation::Same || (launch == d.domain && ((edge == 1 && d.edge == 2) || (edge == 2 && d.edge == 1))))
                    return it->second = false;
            }
            for (const uint32_t cls : crossingClasses.classes(set))
                if (const uint32_t launch = cls >> 2; launch != d.domain && !domainClockName(launch).empty() &&
                    domainClockName(launch) == domainClockName(d.domain))
                    sameNameDomainPairs_.insert({launch, d.domain});
            return true;
        };
        // Exactly one source (the loop below needs `flops.size() + inputs.size() == 1` of the full cone). A summary can only bound it:
        // when one distinct source remains and some were summarised, the full cone decides.
        const auto singleSource = [&](const Flop& d, const ConeView& v) {
            std::vector<NodeId> distinct;
            for (const auto& s : v.flops) distinct.push_back(s.q);
            for (const auto& s : v.inputs) distinct.push_back(s.q);
            bool summarised = false;
            for (const NodeId s : {v.skippedA, v.skippedB})
                if (s != kNoNode && s != ConeView::kMany && s != d.q) { distinct.push_back(s); summarised = true; }
            std::sort(distinct.begin(), distinct.end());
            distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
            if (v.skippedMany || distinct.size() != 1) return false;
            if (!summarised) return v.flops.size() + v.inputs.size() == 1;
            const auto full = coneOf(d);
            return full->flops.size() + full->inputs.size() == 1;
        };
        for (uint32_t di = 0; di < m->flops.size(); di++) {
            const Flop& d = m->flops[di];
            const bool summarise = d.clockResolved && d.otherDomains.empty() && !m->multiSequential.count(d.q);
            const uint64_t key = summarise ? (uint64_t{d.domain} << 2 | static_cast<uint64_t>(d.edge & 3)) : UINT64_MAX;
            const ConeView cone = cones_->view(d, crossingClasses, key, [&](uint32_t set) { return summarise && ignoredBy(d, set); });
            const bool baseUncertain = !cone.bbLeaves.empty() || !cone.unresolved.empty() ||
                                       !cone.uncertainCalls.empty() || !cone.undriven.empty();
            bool incompleteSrc = baseUncertain || !singleSource(d, cone);
            if (!baseUncertain && incompleteSrc && !d.syncResetGuards.empty() && d.clockResolved) {
                std::vector<NodeId> dataInputs;
                bool unexpectedCtrl = false;
                for (auto ei : m->in[d.q]) {
                    const Edge& e = m->edges[ei];
                    if (e.type != EdgeType::Seq || m->isDeadEdge(e)) continue;
                    if (e.ctrl) {
                        if (std::find(d.syncResetGuards.begin(), d.syncResetGuards.end(), e.from) == d.syncResetGuards.end())
                            unexpectedCtrl = true;
                    } else {
                        dataInputs.push_back(e.from);
                    }
                }
                if (!unexpectedCtrl && !dataInputs.empty() &&
                    std::all_of(d.syncResetGuards.begin(), d.syncResetGuards.end(), [&](NodeId g) { return syncResetGuardTimedSame(d, g); })) {
                    Cone dataCone = cones_->ofNodes(dataInputs);
                    if (dataCone.bbLeaves.empty() && dataCone.unresolved.empty() &&
                        dataCone.uncertainCalls.empty() && dataCone.undriven.empty()) {
                        std::set<NodeId> srcs;
                        bool selfRef = false;
                        for (const auto& src : dataCone.flops) {
                            if (src.q == d.q || m->multiSequential.contains(src.q)) selfRef = true;
                            srcs.insert(src.q);
                        }
                        for (const auto& inp : dataCone.inputs) srcs.insert(inp.q);
                        if (!selfRef && srcs.size() == 1) incompleteSrc = false;
                    }
                }
            }
            incompleteSyncSource[di] = incompleteSrc;
            const uint32_t dstId = nameId(d.q);
            for (auto& s : cone.flops) {
                const Flop& sf = m->flops[m->flopOfQ.at(s.q)];
                if (!sf.clockResolved || !d.clockResolved) {
                    unresolvedEnds++;
                    if (!sf.clockResolved && unresolvedClockSignals.size() < 16)
                        unresolvedClockSignals.insert(stripBit(flopName(sf)));
                    if (!d.clockResolved && unresolvedClockSignals.size() < 16)
                        unresolvedClockSignals.insert(stripBit(flopName(d)));
                    continue;
                }
                const auto pair = worstDomains(sf, d, *rel_);
                if (pair.launch == pair.capture && pair.relation == Relation::Same) {
                    if ((sf.edge == 1 && d.edge == 2) || (sf.edge == 2 && d.edge == 1))
                        halfCycle_.emplace(std::make_pair(*names[nameId(s.q)], *names[dstId]), std::make_pair(s.q, d.q));
                    continue;
                }
                const Relation rel = pair.relation;
                if (pair.launch != pair.capture && !domainClockName(pair.launch).empty() &&
                    domainClockName(pair.launch) == domainClockName(pair.capture))
                    sameNameDomainPairs_.insert({pair.launch, pair.capture});
                if (rel == Relation::Same) continue;
                if (rel == Relation::Unknown) { auto key = std::minmax(pair.launch, pair.capture); unknownPairs.insert(key); unknownPairCount[key]++; }
                const uint32_t srcId = nameId(s.q);
                auto*& known = groupOf[uint64_t{srcId} << 32 | dstId];
                if (!known) {
                    known = &groups[std::make_pair(*names[srcId], *names[dstId])];
                    known->srcName = *names[srcId]; known->dstName = *names[dstId];
                    ++crossingGroups;
                }
                auto& g = *known;
                g.srcDomain = pair.launch; g.dstDomain = pair.capture;
                if (g.dstFlops.empty() || g.dstFlops.back() != di) {
                    g.dstFlops.push_back(di);   // registers are visited in order
                    ++crossingDestinationLinks;
                }
                if (g.srcBits.insert(s.q).second) ++crossingSourceBits;
                // Registers are visited in order and a cone lists each source at most twice, adjacently: no other duplicate.
                if (g.sourceCaptures.empty() || g.sourceCaptures.back() != std::make_pair(s.q, d.q)) g.sourceCaptures.emplace_back(s.q, d.q);
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
                if (!d.clockResolved) {
                    unresolvedEnds++;
                    if (unresolvedClockSignals.size() < 16)
                        unresolvedClockSignals.insert(stripBit(flopName(d)));
                    continue;
                }
                auto [domainInputs, fresh] = inputsToDomain.try_emplace({in.q, d.domain});
                if (fresh) ++inputDomainGroups;
                domainInputs->second.push_back(di);
                ++inputCaptureLinks;
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
            if (cfg->timings && (di + 1) % 4096 == 0 && di + 1 != m->flops.size()) {
                checkpointCrossings(di + 1);
            }
        }
        if (cfg->timings) checkpointCrossings(m->flops.size());
        for (auto& [key, g] : groups) std::sort(g.sourceCaptures.begin(), g.sourceCaptures.end());

        // Reassemble bitwise crossings that are elaborated as one synchronizer
        // instance per generate lane. We only collapse a complete, one-to-one
        // mapping of every bit of one source symbol into indexed capture
        // instances under the same parent path. Missing, duplicated, opaque,
        // logic-derived, or differently clocked lanes remain separate groups.
        struct GrayLane {
            std::pair<std::string, std::string> key;
            NodeId source = kNoNode;
            uint32_t capture = 0;
            std::string parentPath;
            std::vector<int64_t> pathIndices;
            int64_t lane = -1;
            uint32_t srcDomain = 0, dstDomain = 0;
            bool logicBefore = false;
        };
        using LaneSetKey = std::tuple<std::string, const slang::ast::ValueSymbol*, std::string>;
        std::map<LaneSetKey, std::vector<GrayLane>> laneSets;
        auto laneTemplate = [](const std::string& path, std::string& parent, std::vector<int64_t>& indices) {
            parent.clear();
            indices.clear();
            for (size_t i = 0; i < path.size(); ++i) {
                if (path[i] != '[') { parent.push_back(path[i]); continue; }
                auto end = path.find(']', i + 1);
                if (end == std::string::npos || end == i + 1) return false;
                int64_t value = 0;
                for (size_t j = i + 1; j < end; ++j) {
                    if (path[j] < '0' || path[j] > '9') return false;
                    const int digit = path[j] - '0';
                    if (value > (std::numeric_limits<int64_t>::max() - digit) / 10) return false;
                    value = value * 10 + digit;
                }
                indices.push_back(value);
                parent += "[*]";
                i = end;
            }
            return !indices.empty();
        };
        for (const auto& [key, g] : groups) {
            if (g.collapsed || g.dstFlops.size() != 1) continue;
            const auto& captureFlop = m->flops[g.dstFlops.front()];
            std::string parent;
            std::vector<int64_t> indices;
            const std::string& path = procOf(captureFlop).instancePath;
            if (!laneTemplate(path, parent, indices)) continue;
            for (NodeId source : g.srcBits) {
                const auto* symbol = m->nodes[source].sym;
                if (!symbol || !m->flopAt(source)) continue;
                laneSets[{g.srcName, symbol, parent}].push_back({key, source, g.dstFlops.front(), parent, indices, -1,
                                                                  g.srcDomain, g.dstDomain, g.anyLogicBefore});
            }
        }
        std::vector<std::pair<std::string, std::string>> removeLanes;
        std::vector<std::pair<std::pair<std::string, std::string>, Group>> addBuses;
        std::vector<Diagnostic> uncertainLaneGroups;
        for (auto& [laneKey, lanes] : laneSets) {
            const auto* symbol = std::get<1>(laneKey);
            const uint64_t width = symbol ? m->symbolWidth(lanes.front().source) : 0;
            if (width < 2 || lanes.size() < 2) continue;
            std::set<uint32_t> sourceBits, captures;
            std::set<int64_t> laneIndices;
            std::set<uint32_t> firstStages;
            const size_t indexCount = lanes.front().pathIndices.size();
            std::vector<std::set<int64_t>> indexValues(indexCount);
            bool indexShapeValid = indexCount != 0;
            for (const auto& lane : lanes) {
                if (lane.pathIndices.size() != indexCount) { indexShapeValid = false; break; }
                for (size_t axis = 0; axis < indexCount; ++axis) indexValues[axis].insert(lane.pathIndices[axis]);
            }
            size_t varyingAxes = 0, varyingAxis = 0;
            for (size_t axis = 0; axis < indexValues.size(); ++axis)
                if (indexValues[axis].size() > 1) { varyingAxes++; varyingAxis = axis; }
            std::optional<std::set<std::tuple<NodeId, bool, bool, int>>> laneResetSignature;
            std::optional<std::pair<uint32_t, uint32_t>> laneClockPair;
            bool complete = indexShapeValid && varyingAxes == 1;
            std::string incompleteReason = complete ? "" : "more than one generate index varies, or no varying index was found";
            Group bus;
            for (const auto& lane : lanes) {
                const auto& g = groups.at(lane.key);
                const uint32_t bit = m->nodes[lane.source].bit;
                const int64_t laneIndex = lane.pathIndices.size() == indexCount ? lane.pathIndices[varyingAxis] : -1;
                if (bit >= width || !sourceBits.insert(bit).second || !captures.insert(lane.capture).second ||
                    !indexShapeValid || varyingAxes != 1 || lane.logicBefore || !laneIndices.insert(laneIndex).second ||
                    m->nodes[lane.source].sym != symbol) { complete = false; incompleteReason = "source bits or capture lanes are duplicated, logic-derived, or have inconsistent index shapes"; break; }
                const auto clockPair = std::make_pair(lane.srcDomain, lane.dstDomain);
                if (!laneClockPair) laneClockPair = clockPair;
                else if (*laneClockPair != clockPair) { complete = false; incompleteReason = "capture lanes use different clock pairs"; break; }
                Chain chain = chainFor(lane.capture);
                if (chain.depth < 2 || chain.dualEdgeStage || chain.callEffectsUnknown || chain.linkLogic || chain.extraFanout || chain.clockMismatch || chain.resetMismatch ||
                    chain.stages.empty() || chain.stages.front() != lane.capture || !m->flops[lane.capture].clockResolved) { complete = false; incompleteReason = "a lane is not a clean full synchronizer chain"; break; }
                const Flop& captureFlop = m->flops[lane.capture];
                const auto capturePair = std::make_pair(lane.source, captureFlop.q);
                if (g.sourceCaptures.size() != 1 || *g.sourceCaptures.begin() != capturePair) { complete = false; incompleteReason = "a lane does not have one exact source-bit to first-stage capture"; break; }
                std::set<std::tuple<NodeId, bool, bool, int>> resetSignature;
                for (size_t ri = 0; ri < captureFlop.resets.size(); ++ri) {
                    const auto& reset = captureFlop.resets[ri];
                    const int value = ri < captureFlop.resetKinds.size() ? captureFlop.resetKinds[ri] : 2;
                    resetSignature.emplace(reset.root.node, reset.async, reset.activeLow, value);
                }
                if (!laneResetSignature) laneResetSignature = resetSignature;
                else if (*laneResetSignature != resetSignature) { complete = false; incompleteReason = "capture lanes have different reset signatures"; break; }
                firstStages.insert(chain.stages.front());
                bus.srcBits.insert(lane.source);
                bus.sourceCaptures.emplace_back(lane.source, m->flops[chain.stages.front()].q);
                bus.dstFlops.push_back(lane.capture);
                bus.anyLogicBefore |= g.anyLogicBefore;
                if (!bus.loc.valid()) { bus.loc = g.loc; bus.hier = lane.parentPath; }
                removeLanes.push_back(lane.key);
            }
            if (complete && (sourceBits.size() != width || firstStages.size() != width)) {
                complete = false;
                incompleteReason = "not every source bit and first-stage capture is present exactly once";
            }
            if (!complete) {
                for (const auto& lane : lanes) removeLanes.erase(std::remove(removeLanes.begin(), removeLanes.end(), lane.key), removeLanes.end());
                if (lanes.size() >= 2 && width > 1) {
                    for (const auto& lane : lanes) groups.at(lane.key).unresolvedLanes = true;
                    const auto& first = groups.at(lanes.front().key);
                    Diagnostic d = mk("CDC-009", Severity::Warning,
                                      "multi-bit source " + first.srcName + " has separate synchronizer lanes under " + lanes.front().parentPath +
                                      ": exact one-to-one bit coverage could not be established (" + incompleteReason + "), so coherence is unresolved",
                                      first.loc, lanes.front().parentPath, Confidence::Unresolved);
                    d.signals = {first.srcName};
                    d.proofState = ProofState::NotAttempted;
                    d.evidence.push_back({"proof_obligation", "every source bit must map to exactly one independently synchronized first-stage capture; incomplete or duplicated lanes cannot establish Gray-bus coherence", first.loc});
                    d.nextStep = "Review each elaborated source-bit to first-stage mapping, or use a recognized coherent crossing protocol.";
                    uncertainLaneGroups.push_back(std::move(d));
                    incomplete("cdc-017", "multi-bit source has incomplete generated synchronizer-lane provenance");
                }
                continue;
            }
            bus.srcName = std::get<0>(laneKey);
            bus.dstName = lanes.front().parentPath;
            bus.srcDomain = lanes.front().srcDomain;
            bus.dstDomain = lanes.front().dstDomain;
            bus.srcQ = bus.srcBits.empty() ? kNoNode : *bus.srcBits.begin();
            bus.realWidth = width;
            std::sort(bus.sourceCaptures.begin(), bus.sourceCaptures.end());
            auto busKey = std::make_pair(bus.srcName, bus.dstName);
            addBuses.push_back({busKey, std::move(bus)});
        }
        for (const auto& key : removeLanes) groups.erase(key);
        for (auto& [key, bus] : addBuses) groups.emplace(key, std::move(bus));
        for (auto& finding : uncertainLaneGroups) add(std::move(finding));

        // ---- pre-pass: chains and the outputs of recognised synchronisers (needed to recognise qualifiers)
        if (cfg->timings) cfg->timings->record("cdc:crossings:loop", crossingStart);
        const auto stageStartChains = Timings::Clock::now();
        for (auto& [key, g] : groups) {
            for (auto di : g.dstFlops) {
                if (!haveChain[di]) { chains[di] = chainFor(di); haveChain[di] = 1; }
                const Chain& ch = chains[di];
                if (!ch.callEffectsUnknown && !ch.linkLogic && !ch.feedbackWithFanout && ch.depth >= 2)
                {
                    // The output stage and every follower are outputs: followers do not change what reads it. A chain short of
                    // the required depth still has an output, marked not full, so a reader can tell (CDC-005 reports the chain).
                    bool full = false;
                    int reached = 0;
                    const size_t first = outputStage(ch, &full);
                    for (size_t stage = 0; stage < ch.stages.size(); ++stage) {
                        const Flop& output = m->flops[ch.stages[stage]];
                        reached += 1 + output.extraStages;
                        if (reached >= 2)
                            (stage >= first ? syncedOut_ : earlyQualifierTap_)[output.q] = {g.srcDomain, g.dstDomain, g.hier, g.srcName, g.srcQ,
                                g.dstFlops.size(), reached, full && stage >= first, ch.stages[0], "", incompleteSyncSource[di] != 0, ch.clockMismatch || ch.resetMismatch, ch.dualEdgeStage};
                    }
                }
            }
        }

        const auto inputIntent = [&](NodeId inNode, uint32_t dom) {
            const sdc::InputDelay* decl = nullptr;
            bool rejectedMode = false;
            auto matchingDecls = inputContractsFor(inNode, &rejectedMode);
            Relation inRel = Relation::Unknown;
            bool contractHasUnknown = false;
            bool hasUnknownMode = false;
            const sdc::InputDelay* unknownDecl = nullptr;
            if (!matchingDecls.empty()) {
                bool allSame = true, allExclusive = true;
                const sdc::InputDelay* asyncDecl = nullptr;
                std::optional<Relation> firstRelation;
                std::set<std::string> launchClocks;
                for (const auto& active : matchingDecls) {
                    const auto rel = rel_->relationToName(dom, active.delay->clock);
                    if (firstRelation && *firstRelation != rel) contractHasUnknown = true;
                    firstRelation = rel;
                    launchClocks.insert(active.delay->clock);
                    allSame &= rel == Relation::Same;
                    allExclusive &= rel == Relation::Exclusive;
                    if (rel == Relation::Async) asyncDecl = active.delay;
                    if (rel == Relation::Unknown) {
                        unknownDecl = active.delay;
                        hasUnknownMode = true;
                        contractHasUnknown = true;
                    } else if (rel == Relation::Exclusive) {
                        contractHasUnknown = true;
                    }
                }
                // Additive and repeated min/max input constraints retain every possible launch relation.
                // An asynchronous launch remains relevant even when a later clause is synchronous.
                inRel = asyncDecl ? Relation::Async : allSame ? Relation::Same : allExclusive ? Relation::Exclusive :
                        Relation::Unknown;
                contractHasUnknown |= launchClocks.size() > 1;
                decl = asyncDecl ? asyncDecl : allSame || allExclusive ? matchingDecls.back().delay : unknownDecl;
            }
            if (rejectedMode) {
                contractHasUnknown = true;
                if (inRel != Relation::Async) {
                    inRel = Relation::Unknown;
                    decl = nullptr;
                }
            }
            return std::tuple{std::move(matchingDecls), inRel, contractHasUnknown, decl, rejectedMode, hasUnknownMode, unknownDecl};
        };
        struct InputBus {
            size_t bits = 0;
            NodeId first = kNoNode;
            size_t asyncBits = 0;
            size_t exclusiveBits = 0;
            NodeId firstAsync = kNoNode;
            Relation relationship() const {
                if (asyncBits >= 2) return Relation::Async;
                return exclusiveBits == bits ? Relation::Exclusive : Relation::Unknown;
            }
        };
        std::unordered_map<uint64_t, InputBus> inputBitsByNameDomain;
        const auto isSyncResetOnlyInput = [&](uint32_t di, NodeId inNode) -> bool {
            const Flop& df = m->flops[di];
            if (df.syncResetGuards.empty()) return false;
            std::vector<NodeId> dataInputs;
            for (auto ei : m->in[df.q]) {
                const Edge& e = m->edges[ei];
                if (e.type == EdgeType::Seq && !e.ctrl && !m->isDeadEdge(e))
                    dataInputs.push_back(e.from);
            }
            if (dataInputs.empty()) return false;
            Cone dataCone = cones_->ofNodes(dataInputs);
            if (dataCone.flops.empty() && dataCone.inputs.empty()) return false;
            return std::none_of(dataCone.inputs.begin(), dataCone.inputs.end(),
                                [&](const ConeSrc& s) { return s.q == inNode; });
        };
        // Collect qualifier sources before classifying any payload, regardless
        // of the order in which elaboration numbered the primary inputs.
        for (const auto& [key, dsts] : inputsToDomain) {
            const auto [inNode, dom] = key;
            bool hasChain = false;
            for (auto di : dsts) {
                if (!haveChain[di]) { chains[di] = chainFor(di); haveChain[di] = 1; }
                const auto& chain = chains[di];
                hasChain |= !chain.callEffectsUnknown && !chain.linkLogic && !chain.feedbackWithFanout && chain.depth >= 2;
            }
            if (!hasChain) continue;
            const auto [matchingDecls, inRel, contractHasUnknown, decl, rejectedMode, hasUnknownMode, unknownDecl] = inputIntent(inNode, dom);
            if (inRel != Relation::Same && !m->collapsed(inNode) && m->symbolWidth(inNode) > 1 &&
                !inputLogic_.contains({inNode, dom}) && std::all_of(dsts.begin(), dsts.end(), [&](uint32_t di) {
                    const Chain& ch = chains[di];
                    return !incompleteSyncSource[di] && !ch.dualEdgeStage && !ch.callEffectsUnknown && !ch.linkLogic &&
                           !ch.feedbackWithFanout && ch.depth >= minStages;
                })) {
                auto& bus = inputBitsByNameDomain[uint64_t{dom} << 32 | nameId(inNode)];
                ++bus.bits;
                bus.first = std::min(bus.first, inNode);
                if (inRel == Relation::Async) {
                    ++bus.asyncBits;
                    bus.firstAsync = std::min(bus.firstAsync, inNode);
                }
                if (inRel == Relation::Exclusive) ++bus.exclusiveBits;
            }
            // Make primary-input synchronizer outputs visible to the shared reconvergence analysis.
            // Register-launched chains are populated in the group pre-pass above; without this companion
            // path independently synchronized bits from one asynchronous input bus disappear at reconvergence.
            // A primary input without an established launch clock has no
            // source domain. Do not let the destination domain used as a
            // bookkeeping default make its synchronizer look same-source.
            if (inRel != Relation::Same) {
                for (auto di : dsts) {
                    const Chain& ch = chains[di];
                    if (ch.callEffectsUnknown || ch.linkLogic || ch.feedbackWithFanout || ch.depth < 2) continue;
                    if (isSyncResetOnlyInput(di, inNode)) continue;
                    bool full = false;
                    int reached = 0;
                    const size_t firstOutput = outputStage(ch, &full);
                    const auto& firstFlop = m->flops[di];
                    uint32_t sourceDomain = dom;
                    if (decl) {
                        for (uint32_t candidate = 0; candidate < m->domains.size(); ++candidate)
                            if (rel_->relationToName(candidate, decl->clock) == Relation::Same) {
                                sourceDomain = candidate;
                                break;
                            }
                    }
                    // For top-level inputs the declared clock name is retained
                    // as source identity when no register in that clock domain
                    // provides a model domain id. Matching then requires the
                    // payload's own launch contract to name the same clock.
                    const bool sourceIncomplete = incompleteSyncSource[di] || matchingDecls.empty() || contractHasUnknown;
                    for (size_t stage = 0; stage < ch.stages.size(); ++stage) {
                        const Flop& output = m->flops[ch.stages[stage]];
                        reached += 1 + output.extraStages;
                        if (reached >= 2) {
                            auto& dstMap = (stage >= firstOutput ? syncedOut_ : earlyQualifierTap_);
                            auto [it, inserted] = dstMap.emplace(output.q, SyncOut{sourceDomain, dom, procOf(firstFlop).instancePath,
                                m->nameOf(inNode), inNode, 1, reached, full && stage >= firstOutput, ch.stages[0],
                                decl ? decl->clock : "", sourceIncomplete, ch.clockMismatch || ch.resetMismatch, ch.dualEdgeStage});
                            if (!inserted && sourceIncomplete) it->second.sourceIncomplete = true;
                        }
                    }
                }
            }
        }
        analyseBoundaryEvents(inputsToDomain);

        const auto isUncleanGrayCapture = [&](const Chain& ch) {
            const bool handledUncertain = (ch.dualEdgeStage && ch.depth >= 2 && !ch.linkLogic) ||
                                          (ch.callEffectsUnknown && ch.depth >= 2);
            return (!handledUncertain && (ch.linkLogic || ch.depth < minStages)) ||
                   (ch.callEffectsUnknown && ch.linkLogic) ||
                   ch.extraFanout || ch.midFanout || ch.leaksDomain;
        };
        const auto sourceHasCounterOrigin = [&](const std::set<NodeId>& srcBits) {
            for (auto q : srcBits) {
                auto fit = m->flopOfQ.find(q);
                if (fit == m->flopOfQ.end()) continue;
                const auto heldCone = coneOf(m->flops[fit->second]);
                const Cone& gc = *heldCone;
                std::set<NodeId> leaves;
                for (auto& x : gc.flops) leaves.insert(x.q);
                if (!leaves.empty() && counterLike(leaves)) return true;
            }
            return false;
        };
        struct SplitGrayInfo {
            size_t groupCount = 0;
            std::set<NodeId> allSrcBits;
            std::set<NodeId> cleanSrcBits;
            std::set<NodeId> uncleanSrcBits;
            std::set<NodeId> uncleanOnlySrcBits;
            bool anyOpaque = false;
        };
        using SplitGrayKey = std::tuple<const slang::ast::ValueSymbol*, uint32_t, uint32_t>;
        std::map<SplitGrayKey, SplitGrayInfo> splitGrayPartialSync;
        {
            std::map<SplitGrayKey, SplitGrayInfo> candidates;
            for (const auto& [key, g] : groups) {
                if (g.collapsed || g.srcBits.empty()) continue;
                const auto* srcSym = m->nodes[*g.srcBits.begin()].sym;
                if (!srcSym || m->symbolWidth(*g.srcBits.begin()) < 2) continue;
                if (!std::all_of(g.srcBits.begin(), g.srcBits.end(),
                                 [&](NodeId q) { return m->nodes[q].sym == srcSym; }))
                    continue;
                auto& entry = candidates[{srcSym, g.srcDomain, g.dstDomain}];
                ++entry.groupCount;
                entry.allSrcBits.insert(g.srcBits.begin(), g.srcBits.end());
                for (NodeId q : g.srcBits)
                    if (hasOpaqueFunction(q)) entry.anyOpaque = true;
                for (const auto& [sq, dq] : g.sourceCaptures) {
                    auto fit = m->flopOfQ.find(dq);
                    if (fit == m->flopOfQ.end()) continue;
                    if (isUncleanGrayCapture(chains[fit->second])) entry.uncleanSrcBits.insert(sq);
                    else entry.cleanSrcBits.insert(sq);
                }
            }
            for (auto& [skey, info] : candidates) {
                for (NodeId sq : info.uncleanSrcBits)
                    if (!info.cleanSrcBits.count(sq)) info.uncleanOnlySrcBits.insert(sq);
                if (info.groupCount < 2 || info.allSrcBits.size() < 2 || info.uncleanOnlySrcBits.empty() || info.anyOpaque)
                    continue;
                const bool allGray = std::all_of(info.allSrcBits.begin(), info.allSrcBits.end(),
                                                 [&](NodeId q) { return isGrayFlop(q); });
                if (allGray && sourceHasCounterOrigin(info.allSrcBits))
                    splitGrayPartialSync.emplace(skey, std::move(info));
            }
        }

        // Synchronised controls by scope (source domain, destination domain, hierarchy), in syncedOut_ order: the inventory is
        // complete here, and every group asks for its scope's controls.
        std::map<std::tuple<uint32_t, uint32_t, std::string>, std::vector<const SyncOut*>> syncedByScope;
        for (const auto& [q, so] : syncedOut_) syncedByScope[{so.srcDomain, so.dstDomain, so.hier}].push_back(&so);
        const auto syncedInScope = [&](const Group& g) -> const std::vector<const SyncOut*>& {
            static const std::vector<const SyncOut*> none;
            const auto it = syncedByScope.find({g.srcDomain, g.dstDomain, g.hier});
            return it == syncedByScope.end() ? none : it->second;
        };
        // ---- per signal-level crossing groups
        if (cfg->timings) cfg->timings->record("cdc:crossings:chains", stageStartChains);
        uint64_t groupsClassified = 0;
        {   // the qualifier's source sets for every capture the groups below ask about, in one batched walk
            std::vector<std::pair<uint32_t, uint32_t>> keys;
            for (const auto& [key, g] : groups)
                if (!g.collapsed)
                    for (const auto di : g.dstFlops) keys.emplace_back(di, g.srcDomain);
            precomputeQualifierSources(std::move(keys));
        }
        for (auto& [key, g] : groups) {
            if (cfg->timings && ++groupsClassified % 1024 == 0)
                cfg->timings->checkpoint({{"cdc.crossings.groups_classified", groupsClassified}, {"cdc.crossings.groups", groups.size()},
                                          {"cdc.qualifier.calls", qualifierCalls_}, {"cdc.qualifier.expansions", qualifierExpansions_}, {"cdc.qualifier.source_sets", qualifierSourceSets_}, {"cdc.qualifier.probe_hits", probeHits_}, {"cdc.qualifier.expansion_clears", qualifierExpansionClears_}, {"cdc.qualifier.source_clears", qualifierSourceClears_}, {"cdc.qualifier.expansion_ms", qualifierNanos_[0] / 1000000},
                                          {"cdc.qualifier.sources_ms", qualifierNanos_[1] / 1000000}, {"cdc.qualifier.writes_ms", qualifierNanos_[2] / 1000000},
                                          {"cdc.witness_ms", witnessNanos_ / 1000000}, {"cdc.witness.calls", witnessCalls_},
                                          {"cdc.witness.positions", witnessPositions_}, {"cdc.witness.expanded", witnessExpanded_},
                                          {"cdc.witness.found", witnessFound_}, {"cdc.witness.edge_scans", witnessEdgeScans_}});
            Relation rel = relation(g.srcDomain, g.dstDomain);
            std::sort(g.dstFlops.begin(), g.dstFlops.end());
            uint64_t width = g.dstFlops.size();
            evaluated["CDC-003"] += width;
            int unsynced = 0, shortChain = 0, logicLink = 0, fanout = 0, synced = 0, callUnknown = 0;
            bool dualEdgeChain = false;
            int minDepth = 99, maxDepth = 0;
            for (auto di : g.dstFlops) {
                if (!haveChain[di]) { chains[di] = chainFor(di); haveChain[di] = 1; }
                const Chain& ch = chains[di];
                if (ch.dualEdgeStage && ch.depth >= 2 && !ch.linkLogic) {
                    dualEdgeChain = true;
                    if (ch.extraFanout) fanout++;
                    continue;
                }
                if (ch.callEffectsUnknown && ch.depth >= 2) {
                    callUnknown++;
                    if (ch.linkLogic) logicLink++;
                    if (ch.extraFanout) fanout++;
                    continue;
                }
                // A shallow modeled path remains an established unsafe sample
                // even when a call adds uncertainty. For a candidate chain,
                // opaque effects prevent claiming either sufficient or
                // insufficient synchronizer depth.
                if (ch.linkLogic) logicLink++;
                else if (ch.depth < 2) unsynced++;
                else if (ch.depth < minStages) shortChain++;
                else synced++;
                if (ch.extraFanout) fanout++;
                if (!ch.callEffectsUnknown && !ch.linkLogic && ch.depth >= 2) { minDepth = std::min(minDepth, ch.depth); maxDepth = std::max(maxDepth, ch.depth); }
            }
            // protocol recognition: Gray-coded source, binary counter source, payload qualified by a synchronised control
            bool gray = false, counter = false, qualified = false, opaqueFunction = false, incompleteGrayLanes = false;
            uint64_t expectedGrayWidth = 0;
            Qualification qualification = Qualification::Disproven;
            std::string qualifierReason;
            if (!g.collapsed) {
                for (auto q : g.srcBits) opaqueFunction = opaqueFunction || hasOpaqueFunction(q);
                gray = !opaqueFunction && !g.srcBits.empty();
                for (auto q : g.srcBits) gray = gray && isGrayFlop(q);
                if (gray) {   // a Gray pointer is the conversion of a counter: the binary side must be a carry-dependent register group
                    gray = sourceHasCounterOrigin(g.srcBits);
                }
                if (gray && width > 1) {
                    const auto* srcSym = m->nodes[*g.srcBits.begin()].sym;
                    expectedGrayWidth = srcSym ? m->symbolWidth(*g.srcBits.begin()) : 0;
                    bool sameSym = srcSym != nullptr;
                    for (auto q : g.srcBits) {
                        if (m->nodes[q].sym != srcSym) { sameSym = false; break; }
                    }
                    if (!sameSym || expectedGrayWidth < 2 || g.srcBits.size() != expectedGrayWidth) {
                        gray = false;
                        incompleteGrayLanes = true;
                    }
                }
                counter = !gray && !opaqueFunction && g.srcBits.size() > 1 && counterLike(g.srcBits);
                qualification = Qualification::ProvenNecessary;
                std::optional<std::set<std::pair<NodeId, bool>>> bundleNecessary;
                std::set<NodeId> bundleQualifierNodes;
                bool sawProvenBit = false;
                for (auto di : g.dstFlops) {
                    auto result = ctrlFromSynced(m->flops[di], g.srcDomain, {}, g.srcBits);
                    if (chains[di].depth >= 2 || result.state == Qualification::Disproven) {
                        qualification = Qualification::Disproven;
                        qualifierReason = result.reason;
                        break;
                    }
                    if (result.state == Qualification::Undecided) {
                        qualification = Qualification::Undecided;
                        qualifierReason = result.reason;
                        continue;
                    }
                    sawProvenBit = true;
                    for (const auto& [qNode, _] : result.necessaryQualifiers) bundleQualifierNodes.insert(qNode);
                    if (!bundleNecessary) bundleNecessary = result.necessaryQualifiers;
                    else {
                        std::set<std::pair<NodeId, bool>> shared;
                        std::set_intersection(bundleNecessary->begin(), bundleNecessary->end(),
                                              result.necessaryQualifiers.begin(), result.necessaryQualifiers.end(),
                                              std::inserter(shared, shared.end()));
                        bundleNecessary = std::move(shared);
                    }
                }
                if (qualification == Qualification::ProvenNecessary && width > 1 && sawProvenBit &&
                    (!bundleNecessary || bundleNecessary->empty())) {
                    qualification = Qualification::Undecided;
                    if (bundleQualifierNodes.size() == 1)
                        qualifierReason = "capture bits of the multi-bit bundle require opposite polarities of synchronized qualifier " +
                                          m->nameOf(*bundleQualifierNodes.begin());
                    else
                        qualifierReason = "capture bits of the multi-bit bundle do not share a single synchronized qualifier and polarity";
                }
                qualified = qualification == Qualification::ProvenNecessary;
            }
            const bool hasSiblingSync = width > 1 && std::any_of(syncedInScope(g).begin(), syncedInScope(g).end(),
                                                                 [&](const SyncOut* so) { return so->srcName != g.srcName; });
            if (hasSiblingSync && !gray && !counter && synced == 0 && shortChain == 0 && logicLink > 0) {
                for (auto di : g.dstFlops) {
                    const Chain& ch = chains[di];
                    if (ch.linkLogic && ch.multiInputLinkLogic && !(ch.callEffectsUnknown && ch.depth >= 2)) {
                        --logicLink;
                        ++unsynced;
                    }
                }
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
                for (auto di : g.dstFlops) noNextStage = noNextStage && !chains[di].callEffectsUnknown && chains[di].depth < 2 && !chains[di].linkLogic;
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
                incomplete("graph-precision", "wide crossing " + g.srcName + " -> " + g.dstName + " not classified");
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
                if (!witness) {
                    const auto witnessStart = Timings::Clock::now();
                    witness = pathWitness(g);
                    witnessNanos_ += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Timings::Clock::now() - witnessStart).count());
                }
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
                unresolvedQualificationPairs_.emplace(g.srcName, g.dstName);
                Diagnostic d = base("CDC-039", Severity::Warning,
                                    "payload qualification is unresolved for " + g.srcName + " -> " + g.dstName + ": " + qualifierReason + tag);
                d.confidence = Confidence::Unresolved;
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"proof_obligation", "prove that every data-moving assignment requires control synchronized from the launch domain into the capture domain; " + qualifierReason, {}});
                d.nextStep = "Review the capture predicate and protocol state; separately establish data stability and timing.";
                add(std::move(d));
                evaluated["CDC-039"] += width;
                incomplete("payload-qualification", g.srcName + " -> " + g.dstName + ": " + qualifierReason);
            }
            if (qualified && unsynced) {
                Diagnostic d = base("CDC-039", Severity::Warning,
                                    "payload " + g.srcName + " -> " + g.dstName + " is captured under a control that is synchronised "
                                    "from the same source domain (qualifier pattern)" + tag);
                d.confidence = Confidence::Heuristic;
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
            if (dualEdgeChain) {
                Diagnostic d = base(width > 1 ? "CDC-004" : "CDC-003", Severity::Warning,
                                    "crossing " + g.srcName + " -> " + g.dstName +
                                    " has a dual-edge stage with unresolved synchronizer timing" + tag);
                d.confidence = Confidence::Unresolved;
                d.proofState = ProofState::NotAttempted;
                d.nextStep = "Review edge timing and metastability containment for the dual-edge register (CDC-043).";
                add(std::move(d));
            }
            if (callUnknown) {
                Diagnostic d = base(width > 1 ? "CDC-004" : "CDC-003", Severity::Warning,
                                    "crossing " + g.srcName + " -> " + g.dstName + " has " +
                                    std::to_string(callUnknown) + " capture(s) whose stage data may be changed by an opaque call" + tag);
                d.confidence = Confidence::Unresolved;
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"uncertain_call_effect", "opaque call effects prevent deciding whether the capture stages preserve the modeled crossing value", g.loc});
                d.nextStep = "Review the call effect or provide a model that establishes which state it can write.";
                add(std::move(d));
                incomplete("graph-precision", "opaque call effects prevent CDC stage classification for " + g.dstName);
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
                std::set<std::string> seenStages;
                for (auto di : g.dstFlops) {
                    const Chain& ch = chains[di];
                    if (!ch.linkLogic || ch.linkTarget == kNoNode) continue;
                    const auto targetIt = m->flopOfQ.find(ch.linkTarget);
                    if (targetIt == m->flopOfQ.end()) continue;
                    const Flop& targetFlop = m->flops[targetIt->second];
                    const std::string targetName = stripBit(flopName(targetFlop));
                    if (seenStages.insert(targetName).second)
                        d.evidence.push_back({"capture_stage", targetName, procOf(targetFlop).loc});
                }
                d.nextStep = "Remove logic between synchroniser stages; place logic after the last stage.";
                add(std::move(d));
            }
            if (fanout && (synced || shortChain || callUnknown || dualEdgeChain)) {
                Diagnostic d = base("CDC-007", sevViol, "first-stage output of the synchroniser " + g.dstName +
                                    " fans out to logic other than the next stage (" + std::to_string(fanout) + " bit(s))" + tag);
                for (auto di : g.dstFlops)
                    for (const auto& target : chains[di].fanoutTargets)
                        d.evidence.push_back({"downstream_tap", target.name, target.loc});
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
                                        "destination domain only gates the flop, it does not synchronise the data" +
                                        (qualifierReason.empty() ? std::string{} : "; " + qualifierReason) + tag);
                    d.nextStep = "Qualify the capture with a control synchronised from the source domain (and hold the data stable), or synchronise the data.";
                    busFacts.push_back(std::move(d));
                    evaluated["CDC-028"] += width;
                }
                // a synchronised control exists between the same domains in the same scope, but this payload is not gated by it
                if (width > 1) for (const SyncOut* so : syncedInScope(g))
                    if (so->srcName != g.srcName) {
                        Diagnostic d = base("CDC-016", sevViol, "payload " + g.srcName + " -> " + g.dstName + " crosses ungated although the control " +
                                            so->srcName + " is synchronised between the same clock domains" + tag);
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
                incomplete("cdc-017", "user-function output prevents classification of " + g.srcName);
            }
            if (width > 1 && (gray || counter)) {
                evaluated["CDC-017"] += width; evaluated["CDC-018"] += width;
                if (counter) {
                    const bool liveBinary = hasLiveBinaryCounterUpdate(g.srcBits);
                    if (liveBinary) {
                        Diagnostic d = base("CDC-017", sevViol, "multi-bit pointer " + g.srcName + " -> " + g.dstName + " is a binary counter: several bits change at "
                                            "once, so independently synchronised bits can be captured in an invalid state" + tag);
                        d.confidence = Confidence::Heuristic;
                        d.nextStep = "Convert the pointer to Gray code before the crossing (single bit changes per increment).";
                        busFacts.push_back(std::move(d));
                    } else {
                        const Severity sevUncertain = rel == Relation::Exclusive ? Severity::Info : Severity::Warning;
                        Diagnostic d = base("CDC-017", sevUncertain, "multi-bit crossing " + g.srcName + " -> " + g.dstName +
                                            " has cross-bit state dependence, but binary-counter vs. unit-step encoding is not proven" + tag);
                        d.confidence = Confidence::Unresolved;
                        d.proofState = ProofState::NotAttempted;
                        d.evidence.push_back({"proof_obligation", "verify whether at most one bit changes per transition (e.g. Gray or Johnson encoding) or add a coherent bus protocol", {}});
                        d.nextStep = "Review the state encoding and transitions, or convert to a recognised Gray pointer or qualified bus.";
                        busFacts.push_back(std::move(d));
                    }
                }
                if (gray) {
                    int unclean = 0;
                    for (auto di : g.dstFlops)
                        if (isUncleanGrayCapture(chains[di])) unclean++;
                    if (unclean) {
                        Diagnostic d = base("CDC-017", sevViol, "Gray-coded pointer " + g.srcName + " -> " + g.dstName +
                                            " is not fully synchronised (" + std::to_string(unclean) + " of " +
                                            std::to_string(width) + " bit(s) lack a clean multi-stage synchroniser)" + tag);
                        d.nextStep = "Every pointer bit needs its own synchroniser chain of the required depth with no logic between stages or early-stage taps.";
                        busFacts.push_back(std::move(d));
                    }
                    if (synced == static_cast<int>(width)) {
                        bool sourcePeriodUnitKnown = false;
                        const double sourcePeriod = periodNs(g.srcDomain, &sourcePeriodUnitKnown);
                        const bool ambiguousClock = rel_->hasMultipleClocks(g.srcDomain) || rel_->hasMultipleClocks(g.dstDomain);
                        using ExceptionKind = sdc::PathException::Kind;
                        const auto hasUnsupportedRef = [](const auto& refs) {
                            return std::any_of(refs.begin(), refs.end(),
                                               [](const auto& ref) { return ref.regexp || ref.unsupportedOption; });
                        };
                        const bool ambiguousException = std::any_of(cfg->constraints.exceptions.begin(), cfg->constraints.exceptions.end(),
                            [](const sdc::PathException& ex) {
                                if (ex.kind != ExceptionKind::FalsePath && ex.kind != ExceptionKind::MaxDelay) return false;
                                const auto unresolvedRef = [](const auto& refs) {
                                    return std::any_of(refs.begin(), refs.end(),
                                                       [](const auto& ref) { return ref.regexp || ref.unsupportedOption; });
                                };
                                // A through-only, regexp, or unsupported-option cut cannot be resolved to endpoints by this matcher.
                                return (ex.from.empty() && ex.to.empty()) || unresolvedRef(ex.from) || unresolvedRef(ex.to) || unresolvedRef(ex.through);
                            }) ||
                            std::any_of(cfg->constraints.inputDelays.begin(), cfg->constraints.inputDelays.end(),
                                        [&](const auto& d) { return hasUnsupportedRef(d.ports); }) ||
                            std::any_of(cfg->constraints.outputDelays.begin(), cfg->constraints.outputDelays.end(),
                                        [&](const auto& d) { return hasUnsupportedRef(d.ports); }) ||
                            std::any_of(cfg->constraints.clocks.begin(), cfg->constraints.clocks.end(),
                                        [&](const auto& c) { return hasUnsupportedRef(c.sources); });
                        const auto timingCommand = [](std::string_view name) {
                            if (const auto sep = name.rfind("::"); sep != std::string_view::npos)
                                name.remove_prefix(sep + 2);
                            return name == "create_clock" || name == "create_generated_clock" || name == "set_units" ||
                                   name == "set_clock_groups" || name == "set_false_path" ||
                                   name == "set_max_delay" || name == "set_min_delay" || name == "set_multicycle_path" ||
                                   name == "set_input_delay" || name == "set_output_delay" || name == "set_disable_timing" ||
                                   name == "reset_path" || name == "remove_clock_groups" || name == "remove_clock" ||
                                   name == "source" || name == "read_sdc" || name == "eval" || name == "uplevel" ||
                                   name == "if" || name == "foreach" || name == "for" || name == "while" ||
                                   name == "switch" || name == "namespace" || name == "apply" || name == "catch" ||
                                   name == "try";
                        };
                        const bool unparsedTiming = cfg->constraints.unreadableFiles ||
                            !cfg->constraints.unsupported.empty() ||
                            std::any_of(cfg->constraints.problems.begin(), cfg->constraints.problems.end(),
                            [&](const auto& problem) { return problem.command.empty() || timingCommand(problem.command); });
                        const bool clockGroupCut = rel == Relation::Async || rel == Relation::Exclusive ||
                            rel_->hasClockGroupCut(g.srcDomain, g.dstDomain);
                        const auto establishedPath = [&](NodeId sq, NodeId dq) -> const sdc::PathException* {
                            const sdc::PathException* bound = nullptr;
                            for (const auto* ex : exceptionsFor(sq, dq, false, ExceptionKind::MaxDelay)) {
                                // Without a precedence model, every competing match must bound the pair.
                                if (ex->from.empty() || ex->to.empty() || !ex->through.empty() || !ex->datapathOnly || ex->edgeQualified ||
                                    ex->resetPath || ex->hold || !sourcePeriodUnitKnown ||
                                    !ex->valueTimeUnitKnown || ex->valueNs < 0 || sourcePeriod <= 0 || ex->valueNs > sourcePeriod)
                                    return nullptr;
                                bound = ex;
                            }
                            return bound;
                        };
                        const sdc::PathException* representative = nullptr;
                        std::set<NodeId> pairedSources, pairedCaptures;
                        std::optional<std::vector<SyncResetGuard>> siblingResetGuards;
                        std::optional<std::set<std::pair<NodeId, bool>>> siblingAsyncResets;
                        std::optional<uint8_t> siblingCaptureEdge;
                        bool resetGuardMismatch = false;
                        bool captureEdgeMismatch = false;
                        bool malformedChain = false;
                        for (auto fi : g.dstFlops) {
                            const Flop& flop = m->flops[fi];
                            std::vector<SyncResetGuard> guards = flop.syncResetFacts;
                            std::sort(guards.begin(), guards.end());
                            guards.erase(std::unique(guards.begin(), guards.end()), guards.end());
                            if (!siblingResetGuards) siblingResetGuards = guards;
                            else if (*siblingResetGuards != guards) resetGuardMismatch = true;
                            const auto asyncResets = asyncResetSignature(flop);
                            if (flop.resets.size() != asyncResets.size()) resetGuardMismatch = true;
                            if (!siblingAsyncResets) siblingAsyncResets = asyncResets;
                            else if (*siblingAsyncResets != asyncResets) resetGuardMismatch = true;
                            if (flop.edge == 3) captureEdgeMismatch = true;
                            if (flop.hasIff) malformedChain = true;
                            if (!siblingCaptureEdge) siblingCaptureEdge = flop.edge;
                            else if (*siblingCaptureEdge != flop.edge) captureEdgeMismatch = true;
                            const auto& ch = chains[fi];
                            for (auto st : ch.stages)
                                if (m->flops[st].hasIff) malformedChain = true;
                            if (ch.clockMismatch || ch.resetMismatch || ch.extraFanout || ch.midFanout || ch.leaksDomain)
                                malformedChain = true;
                        }
                        bool overlappingCut = clockGroupCut || ambiguousException || unparsedTiming;
                        bool complete = !ambiguousClock && !g.srcBits.empty() && !g.dstFlops.empty() &&
                            g.sourceCaptures.size() == g.srcBits.size() && g.sourceCaptures.size() == g.dstFlops.size();
                        complete &= !resetGuardMismatch && !captureEdgeMismatch && !malformedChain;
                        for (const auto& [sq, dq] : g.sourceCaptures) {
                            pairedSources.insert(sq);
                            pairedCaptures.insert(dq);
                            if (!overlappingCut && !exceptionsFor(sq, dq, false, ExceptionKind::FalsePath).empty())
                                overlappingCut = true;
                            const auto* ex = establishedPath(sq, dq);
                            if (!ex) complete = false;
                            else if (!representative) representative = ex;
                        }
                        std::set<NodeId> expectedCaptures;
                        for (auto di : g.dstFlops) expectedCaptures.insert(m->flops[di].q);
                        complete &= !overlappingCut && pairedSources == g.srcBits && pairedCaptures == expectedCaptures;
                        const bool unitStepEvidence = !complete || grayUnitStep(g.srcBits);
                        complete &= unitStepEvidence;
                        if (!complete) {
                            Diagnostic d = base("CDC-018", Severity::Warning,
                                                "Gray-coded crossing " + g.srcName + " -> " + g.dstName +
                                                (resetGuardMismatch ? " has inconsistent first-stage reset guards across Gray siblings" :
                                                 captureEdgeMismatch ? " has inconsistent capture clock edges across Gray siblings" :
                                                 malformedChain ? " has a malformed destination synchroniser chain (stage mismatch or tap)" :
                                                 !unitStepEvidence ? " lacks compatible Gray source transition or reset evidence" :
                                                 " lacks an established max-delay bound for every source-bit/first-stage path "
                                                 "(requires explicit time units, -datapath_only, complete endpoint coverage, and a delay no greater than the source period)") + tag);
                            d.confidence = Confidence::Unresolved;
                            d.proofState = ProofState::NotAttempted;
                            if (overlappingCut)
                                d.evidence.push_back({"constraint_conflict", "a timing exception can override the datapath max-delay or cannot be resolved; the bound is not trusted", {}});
                            if (ambiguousClock) {
                                incomplete("constraints", "multiple active clocks on a Gray crossing clock root");
                                d.evidence.push_back({"clock_intent", "multiple active clock declarations match a crossing root; the bound is not established across those clocks", {}});
                            }
                            d.evidence.push_back({"proof_obligation", "the Gray property, direct unit-step source transition, bus skew and FIFO/protocol correctness are not proven; a partial, unit-unknown or oversized delay is not accepted as evidence", {}});
                            if (resetGuardMismatch)
                                d.evidence.push_back({"reset_obligation", "each affected Gray sibling must have the same trusted destination-local forcing guards; the analyzer does not infer reset coherency", {}});
                            d.nextStep = overlappingCut ? "Review exception precedence and show that the pointer paths retain an effective timing bound."
                                : !unitStepEvidence ? "Review the source counter updates and Gray sampling cadence."
                                : "Add set_max_delay -datapath_only (or a bus skew constraint) on the pointer bits.";
                            add(std::move(d));
                        } else {
                            Diagnostic d = base("CDC-018", Severity::Info, "Gray-coded crossing " + g.srcName + " -> " + g.dstName +
                                                " has complete explicit-unit datapath max-delay coverage within the source period (" + representative->loc.file + ":" + std::to_string(representative->loc.line) +
                                                "); the constraint is trusted, and Gray/FIFO functional correctness is not proven" + tag);
                            d.confidence = Confidence::Heuristic;
                            d.proofState = ProofState::NotAttempted;
                            d.evidence.push_back({"trust_model", "the explicit-unit max-delay and directly observed unit-step counter update are trusted; bus skew, implementation timing and FIFO protocol properties are not verified", {}});
                            add(std::move(d));
                        }
                    }
                }
            } else if (synced && width > 1) {
                if (incompleteGrayLanes) {
                    Diagnostic d = base("CDC-009", exclusive ? Severity::Info : Severity::Warning,
                                        "Gray pointer " + g.srcName + " -> " + g.dstName + " has incomplete capture lane coverage (" +
                                        std::to_string(g.srcBits.size()) + " of " + std::to_string(expectedGrayWidth) +
                                        " source bit(s)): coherence is unresolved" + tag);
                    d.confidence = Confidence::Unresolved;
                    d.proofState = ProofState::NotAttempted;
                    d.evidence.push_back({"proof_obligation", "every source bit must map to an independently synchronized capture lane; an incomplete lane set cannot establish Gray-bus coherence", g.loc});
                    d.nextStep = "Verify that every bit of the Gray pointer is captured by a clean synchroniser lane and covered by a combined timing bound.";
                    add(std::move(d));
                    evaluated["CDC-009"] += width;
                    incomplete("cdc-017", "Gray pointer " + g.srcName + " has incomplete capture lane coverage (" +
                               std::to_string(g.srcBits.size()) + " of " + std::to_string(expectedGrayWidth) + " bits)");
                } else {
                    Diagnostic d = base("CDC-009", exclusive ? Severity::Info : Severity::Warning,
                                        std::to_string(width) + " bits of " + g.srcName + " are synchronised independently into " + g.dstName +
                                        ": bit skew can capture an inconsistent value (coherency not provable structurally)" + tag);
                    d.proofState = ProofState::NotAttempted;
                    d.nextStep = "Prove the bus is Gray-coded / one-hot / held stable by a synchronised qualifier, or use a handshake or async FIFO. Proof obligation: single-bit-change or stability.";
                    add(std::move(d));
                    evaluated["CDC-009"] += width;
                }
            }
            if (!(width > 1 && (gray || counter)) && !g.collapsed && !g.srcBits.empty()) {
                const auto* srcSym = m->nodes[*g.srcBits.begin()].sym;
                if (srcSym && std::all_of(g.srcBits.begin(), g.srcBits.end(),
                                          [&](NodeId q) { return m->nodes[q].sym == srcSym; })) {
                    if (auto sit = splitGrayPartialSync.find({srcSym, g.srcDomain, g.dstDomain}); sit != splitGrayPartialSync.end()) {
                        const auto& split = sit->second;
                        bool hasUncleanOnlyBit = false;
                        for (const auto& [sq, dq] : g.sourceCaptures) {
                            if (!split.uncleanOnlySrcBits.count(sq)) continue;
                            auto fit = m->flopOfQ.find(dq);
                            if (fit != m->flopOfQ.end() && isUncleanGrayCapture(chains[fit->second])) {
                                hasUncleanOnlyBit = true;
                                break;
                            }
                        }
                        if (hasUncleanOnlyBit) {
                            evaluated["CDC-017"] += width;
                            Diagnostic d = base("CDC-017", sevViol,
                                                "Gray-coded pointer " + g.srcName + " -> " + g.dstName +
                                                " is not fully synchronised across its capture registers (" +
                                                std::to_string(split.uncleanOnlySrcBits.size()) + " of " + std::to_string(split.allSrcBits.size()) +
                                                " captured bit(s) lack a clean multi-stage synchroniser)" + tag);
                            d.nextStep = "Every pointer bit needs its own synchroniser chain of the required depth with no logic between stages or early-stage taps.";
                            busFacts.push_back(std::move(d));
                        }
                    }
                }
            }
            if (synced && width == 1 && !unsynced && !logicLink && !fanout &&
                !chains[g.dstFlops.front()].clockMismatch && !chains[g.dstFlops.front()].resetMismatch) {
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
        std::map<std::tuple<NodeId, uint32_t, uint32_t>, QualifierResult> inputQualCache;
        using InputBundleKey = std::tuple<uint32_t, std::string, uint32_t>;
        struct InputBundleInfo {
            size_t bits = 0;
            std::optional<std::set<std::pair<NodeId, bool>>> commonQualifiers;
            std::set<NodeId> qualifierNodes;
        };
        std::map<InputBundleKey, InputBundleInfo> inputBundleQuals;
        for (auto& [k, dsts] : inputsToDomain) {
            auto [inNode, dom] = k;
            std::vector<uint32_t> uniqueDsts = dsts;
            std::sort(uniqueDsts.begin(), uniqueDsts.end());
            uniqueDsts.erase(std::unique(uniqueDsts.begin(), uniqueDsts.end()), uniqueDsts.end());
            auto [matchingDecls, inRel, contractHasUnknown, decl, rejectedMode, hasUnknownMode, unknownDecl] = inputIntent(inNode, dom);
            if (inRel != Relation::Async) continue;
            uint32_t qualifierDomain = dom;
            if (decl) for (uint32_t candidate = 0; candidate < m->domains.size(); ++candidate)
                if (rel_->relationToName(candidate, decl->clock) == Relation::Same) { qualifierDomain = candidate; break; }
            for (auto di : uniqueDsts) {
                const auto& chain = chains[di];
                if (chain.depth >= 2 && !chain.linkLogic && !chain.feedbackWithFanout) continue;
                const auto path = inputPath(inNode, m->flops[di].q);
                if (path.uncertain || !path.data || path.control) continue;
                auto qual = ctrlFromSynced(m->flops[di], qualifierDomain, decl ? decl->clock : std::string{}, {inNode});
                if (qual.state == Qualification::ProvenNecessary && !contractHasUnknown) {
                    auto& info = inputBundleQuals[{nameId(inNode), stripBit(flopName(m->flops[di])), dom}];
                    ++info.bits;
                    for (const auto& [qNode, _] : qual.necessaryQualifiers) info.qualifierNodes.insert(qNode);
                    if (!info.commonQualifiers) info.commonQualifiers = qual.necessaryQualifiers;
                    else {
                        std::set<std::pair<NodeId, bool>> shared;
                        std::set_intersection(info.commonQualifiers->begin(), info.commonQualifiers->end(),
                                              qual.necessaryQualifiers.begin(), qual.necessaryQualifiers.end(),
                                              std::inserter(shared, shared.end()));
                        info.commonQualifiers = std::move(shared);
                    }
                }
                inputQualCache.emplace(std::make_tuple(inNode, dom, di), std::move(qual));
            }
        }
        std::map<InputBundleKey, std::string> incoherentInputBundles;
        for (const auto& [bKey, info] : inputBundleQuals) {
            if (info.bits >= 2 && (!info.commonQualifiers || info.commonQualifiers->empty())) {
                incoherentInputBundles[bKey] = info.qualifierNodes.size() == 1
                    ? "capture bits of the multi-bit input bundle require opposite polarities of synchronized qualifier " +
                      m->nameOf(*info.qualifierNodes.begin())
                    : "capture bits of the multi-bit input bundle do not share a single synchronized qualifier and polarity";
            }
        }
        std::set<std::string> inputCoverage;   // CST-011: inputs reported for a missing input delay, once each
        std::set<std::pair<std::string, uint32_t>> inputReconvergenceReported;
        for (auto& [k, dsts] : inputsToDomain) {
            auto [inNode, dom] = k;
            std::sort(dsts.begin(), dsts.end());
            dsts.erase(std::unique(dsts.begin(), dsts.end()), dsts.end());
            int synced = 0;
            std::vector<uint32_t> shortCaptures;
            for (auto di : dsts) {
                if (!haveChain[di]) { chains[di] = chainFor(di); haveChain[di] = 1; }
                const auto& chain = chains[di];
                if (chain.dualEdgeStage || chain.callEffectsUnknown || chain.linkLogic || chain.feedbackWithFanout ||
                    chain.clockMismatch || chain.resetMismatch || chain.depth < 2) continue;
                if (chain.depth < minStages) shortCaptures.push_back(di);
                else synced++;
            }
            evaluated["CDC-029"] += dsts.size();
            const Flop& f0 = m->flops[dsts[0]];
            bool allSynced = synced == static_cast<int>(dsts.size());
            const bool allChainsRecognised = synced + shortCaptures.size() == dsts.size();
            // set_input_delay -clock C declares the input synchronous to C: relation of C to the capturing clock decides
            std::string inFull = stripBit(m->nameOf(inNode));
            auto [matchingDecls, inRel, contractHasUnknown, decl, rejectedMode, hasUnknownMode, unknownDecl] = inputIntent(inNode, dom);
            if (rejectedMode || hasUnknownMode || (inRel == Relation::Unknown && !matchingDecls.empty())) {
                const std::string captureClock = m->domains[dom].name;
                const std::string reason = rejectedMode ? "an effective input-delay mode was rejected"
                    : hasUnknownMode && unknownDecl
                        ? "capture relationship to the declared launch clock '" + unknownDecl->clock + "' is unknown"
                        : "active input-delay modes disagree on the capture relationship (mixed same and exclusive clocks)";
                Diagnostic unknown = mk("CST-004", Severity::Warning, "input '" + inFull + "' into '" + captureClock +
                                        "': " + reason, procOf(f0).loc, procOf(f0).instancePath, Confidence::Unresolved);
                unknown.identityKey = inFull + ":" + captureClock + ":" + (unknownDecl ? unknownDecl->clock : "");
                unknown.signals = {inFull};
                ++evaluated["CST-004"];
                add(std::move(unknown));
                incomplete("clock-relationships", reason + " for input '" + inFull + "' into '" + captureClock + "'");
            }
            const bool resetOrClockMismatch = std::any_of(dsts.begin(), dsts.end(), [&](uint32_t di) {
                const auto& ch = chains[di];
                return ch.clockMismatch || ch.resetMismatch;
            });
            if (resetOrClockMismatch && inRel != Relation::Same) {
                const Severity mismatchSeverity = inRel == Relation::Async ? Severity::Error :
                    inRel == Relation::Exclusive ? Severity::Info : Severity::Warning;
                Diagnostic mismatch = mk("CDC-026", mismatchSeverity,
                    "primary input " + inFull + " synchroniser stages are not clocked/reset alike (" +
                    (std::any_of(dsts.begin(), dsts.end(), [&](uint32_t di) { return chains[di].clockMismatch; }) ? "different clock edge" : "different asynchronous reset") +
                    " between stages)", procOf(f0).loc, procOf(f0).instancePath,
                    inRel == Relation::Unknown ? Confidence::Unresolved :
                    inRel == Relation::Exclusive ? Confidence::Informational : Confidence::Established);
                mismatch.signals = {inFull};
                mismatch.nextStep = "Clock every stage on the same edge of the destination clock and give all stages equivalent asynchronous resets.";
                add(std::move(mismatch));
                evaluated["CDC-026"] += dsts.size();
                if (inRel == Relation::Unknown) incomplete("clock-relationships", "primary-input synchronizer mismatch for '" + inFull + "' has unknown launch relation");
            }
            if (resetOrClockMismatch) allSynced = false;
            // CDC-047 also applies when the launch object is a primary input:
            // independent first-stage flops can resolve the same input differently
            // for one destination edge just as two register-launched chains can.
            if (inRel != Relation::Same) {
                std::map<std::string, std::vector<uint32_t>> inputChains;
                for (auto di : dsts) {
                    const Chain& ch = chains[di];
                    if (ch.callEffectsUnknown || ch.linkLogic || ch.feedbackWithFanout || ch.depth < 2) continue;
                    inputChains[flopName(m->flops[di])].push_back(di);
                }
                if (inputChains.size() >= 2) {
                    std::string captures;
                    for (const auto& [name, flops] : inputChains) {
                        if (!captures.empty()) captures += ", ";
                        captures += name;
                    }
                    const auto& firstCapture = m->flops[inputChains.begin()->second.front()];
                    Diagnostic duplicate = mk("CDC-047", inRel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                        "primary input " + inFull + " is synchronised independently into " + captures + " in clock domain '" +
                        m->domains[dom].name + "': the copies can differ for a cycle, so logic that combines them can see a value that never existed",
                        procOf(firstCapture).loc, procOf(firstCapture).instancePath,
                        inRel == Relation::Unknown ? Confidence::Unresolved : Confidence::Heuristic);
                    duplicate.identityKey = inFull + ":" + m->domains[dom].name + ":" + relName(inRel) + ":" + captures;
                    duplicate.signals = {inFull};
                    for (const auto& [name, flops] : inputChains) {
                        duplicate.signals.push_back(name);
                        const auto& capture = m->flops[flops.front()];
                        duplicate.evidence.push_back({"capture_chain", name, procOf(capture).loc});
                    }
                    duplicate.proofState = ProofState::NotAttempted;
                    if (inRel == Relation::Exclusive)
                        duplicate.evidence.push_back({"trust_model", "the launch and capture clocks are declared exclusive by constraint (trusted, not verified)", {}});
                    duplicate.nextStep = "Synchronise once and fan out after the last stage, or show that no logic combines the copies.";
                    add(std::move(duplicate));
                    evaluated["CDC-047"]++;
                }
            }
            // With constraints supplied, an input without an input delay has no declared launch clock (CST-011, I/O coverage).
            if (matchingDecls.empty() && cfg->constraintsProvided && inputCoverage.insert(inFull).second) {
                evaluated["CST-011"]++;
                Diagnostic cov = mk("CST-011", Severity::Warning, "input '" + m->nameOf(inNode) + "' is captured by registers but has no set_input_delay: the clock that launches it "
                                    "is not declared, so it is analysed as asynchronous to every clock", procOf(m->flops[dsts[0]]).loc, procOf(m->flops[dsts[0]]).instancePath,
                                    Confidence::Unresolved);
                cov.signals = {inFull};
                cov.nextStep = "Add set_input_delay -clock <launch clock> for it, or declare it asynchronous and synchronise it.";
                add(std::move(cov));
                incomplete("constraints", "input '" + inFull + "' has no input delay");
            }
            bool declaredSync = decl && inRel == Relation::Same;
            std::string contract = decl ? "set_input_delay -clock " + decl->clock + " (" + decl->loc.file + ":" + std::to_string(decl->loc.line) + ") relates it to " +
                                              (declaredSync ? "this clock: declared synchronous" : "a clock that is " + std::string(relName(inRel)) + " to this one")
                                        : rejectedMode ? "an effective input-delay mode was rejected, leaving the launch relationship unresolved"
                                        : matchingDecls.empty() ? "no input-timing/CDC contract (set_input_delay) relates it to that clock"
                                                               : "active input timing contracts disagree on the relationship to this clock";
            // Classify each capture independently. The graph is a may-depend graph;
            // only a precise input-to-D path can establish an unsafe sample. A
            // synchronised qualifier makes an unsynchronised data capture a payload
            // protocol obligation, while opaque/approximate paths stay unresolved.
            std::vector<uint32_t> unsafeCaptures, uncertainCaptures, qualifiedCaptures, undecidedQualifiers;
            uint32_t qualifierDomain = dom;
            if (decl) for (uint32_t candidate = 0; candidate < m->domains.size(); ++candidate)
                if (rel_->relationToName(candidate, decl->clock) == Relation::Same) { qualifierDomain = candidate; break; }
            for (auto di : dsts) {
                const auto& chain = chains[di];
                if (chain.depth >= 2 && !chain.linkLogic && !chain.feedbackWithFanout) {
                    if (chain.dualEdgeStage || chain.callEffectsUnknown || chain.clockMismatch || chain.resetMismatch)
                        uncertainCaptures.push_back(di);
                    continue;
                }
                if (inRel == Relation::Unknown || inRel == Relation::Exclusive) {
                    uncertainCaptures.push_back(di);
                    continue;
                }
                if (inRel != Relation::Async) continue;
                // Only an established asynchronous relation can be promoted
                // to ERROR based on a precise data or control dependency.
                const auto path = inputPath(inNode, m->flops[di].q);
                if (path.uncertain || (!path.data && !path.control)) {
                    uncertainCaptures.push_back(di);
                    continue;
                }
                if (path.data && !path.control) {
                    auto qIt = inputQualCache.find({inNode, dom, di});
                    const auto qualification = qIt != inputQualCache.end() ? qIt->second
                        : ctrlFromSynced(m->flops[di], qualifierDomain, decl ? decl->clock : std::string{}, {inNode});
                    const auto incoherentIt = incoherentInputBundles.find({nameId(inNode), stripBit(flopName(m->flops[di])), dom});
                    // A proof is only as good as every effective launch mode.
                    // One favorable mode cannot establish source identity when
                    // another active min/max contract is unknown or exclusive.
                    if (qualification.state == Qualification::ProvenNecessary && (contractHasUnknown || incoherentIt != incoherentInputBundles.end())) {
                        uncertainCaptures.push_back(di);
                        undecidedQualifiers.push_back(di);
                    }
                    else if (qualification.state == Qualification::ProvenNecessary) qualifiedCaptures.push_back(di);
                    else if (qualification.state == Qualification::Undecided) {
                        uncertainCaptures.push_back(di);
                        undecidedQualifiers.push_back(di);
                    }
                    else {
                        // Only a direct self-hold data arm can make the mux
                        // branch act as a guarded capture. Feedback through
                        // an expression (for example q ^ data) is not a hold.
                        const NodeId capture = m->flops[di].q;
                        const bool feedback = m->selfExact.contains(capture) || m->selfLogic.contains(capture);
                        const bool selfHoldMux = feedback && hasExactInputSelfHold(*m, capture);
                        if (selfHoldMux) {
                            uncertainCaptures.push_back(di);
                            undecidedQualifiers.push_back(di);
                        } else unsafeCaptures.push_back(di);
                    }
                } else unsafeCaptures.push_back(di); // an asynchronous control is itself sampled
            }
            const bool establishedUnsafeSample = !unsafeCaptures.empty();
            const bool allClassifiedSafe = unsafeCaptures.empty() && uncertainCaptures.empty();
            const bool uncertainContract = rejectedMode || hasUnknownMode;
            const Flop& reportFlop = m->flops[establishedUnsafeSample ? unsafeCaptures.front() : dsts.front()];
            Severity sev = declaredSync ? Severity::Info : establishedUnsafeSample ? Severity::Error :
                           allClassifiedSafe && !uncertainContract ? Severity::Info : Severity::Warning;
            Diagnostic d = mk("CDC-029", sev,
                              "top-level input '" + inFull + "' is captured in clock domain '" + m->domains[dom].name + "' by " +
                              std::to_string(establishedUnsafeSample ? unsafeCaptures.size() : dsts.size()) + " register bit(s)" +
                              (declaredSync ? " under a declared synchronous contract" :
                               allSynced ? " through a recognised synchroniser" : establishedUnsafeSample ? " with a precise unsynchronised capture" :
                               allChainsRecognised ? " through a recognised chain below the required depth (CDC-005)" :
                               !qualifiedCaptures.empty() && uncertainCaptures.empty() ? " under a recognised synchronised payload qualifier" : " with capture classification unresolved") + "; " + contract +
                              (establishedUnsafeSample ? "; the explicitly asynchronous input is sampled without metastability containment" : ""),
                              procOf(reportFlop).loc, procOf(reportFlop).instancePath,
                              establishedUnsafeSample ? Confidence::Established :
                                  declaredSync || (allClassifiedSafe && !uncertainContract) ? Confidence::Heuristic : Confidence::Unresolved);
            // What a waiver of this input accepted: changing the capture clock, edge or the declared contract must stale it.
            const std::string inputIdentity = stripBit(m->nameOf(inNode)) + ":" + endpoint(reportFlop, inFull, dom).clockDomain + ":" +
                endpoint(reportFlop, inFull, dom).edge + ":" + (decl ? decl->clock : "") + ":" + relName(inRel);
            Crossing inputCrossing;
            inputCrossing.launch.signal = inFull;
            if (decl) inputCrossing.launch.sdcClock = decl->clock;
            inputCrossing.capture = endpoint(reportFlop, stripBit(flopName(reportFlop)), dom);
            inputCrossing.relationship = relationshipName(inRel);
            inputCrossing.widthBits = establishedUnsafeSample ? unsafeCaptures.size() : dsts.size();
            inputCrossing.logicOnPath = inputLogic_.count({inNode, dom}) != 0;
            d.identityKey = inputIdentity;
            d.crossing = inputCrossing;
            d.signals = {inFull};
            for (const auto& active : matchingDecls)
                d.evidence.push_back({"input_delay_contract", inputContractText(active), active.delay->loc});
            if (m->nameOf(inNode) != inFull)
                d.evidence.push_back({"input_bit", m->nameOf(inNode), {}});
            if (inRel == Relation::Async && contractHasUnknown)
                d.evidence.push_back({"mixed_clock_intent", "an active asynchronous input-delay contract establishes the crossing; effective contracts do not establish a single launch relationship", {}});
            if (auto outputs = topOutputAliases.find(aliasRoot(inNode)); outputs != topOutputAliases.end())
                for (const auto& output : outputs->second)
                    d.evidence.push_back({"downstream_tap", output.name, output.loc});
            d.evidence.push_back({"capture_register", flopName(reportFlop), procOf(reportFlop).loc});
            for (auto di : unsafeCaptures) if (m->flops[di].q != reportFlop.q)
                d.evidence.push_back({"unsafe_capture_register", flopName(m->flops[di]), procOf(m->flops[di]).loc});
            // A shared unresolved clock contract is already described by the group
            // crossing. Preserve each procedure's location for review and site
            // matching, but list individual bits only when we evaluated their paths.
            if (inRel == Relation::Async) {
                for (auto di : uncertainCaptures)
                    d.evidence.push_back({"uncertain_capture_register", flopName(m->flops[di]) + ": dependency or qualification is not established", procOf(m->flops[di]).loc});
            } else if (!uncertainCaptures.empty()) {
                std::unordered_set<uint32_t> reportedProcedures{reportFlop.proc};
                for (auto di : uncertainCaptures) {
                    const auto& capture = m->flops[di];
                    if (reportedProcedures.insert(capture.proc).second)
                        d.evidence.push_back({"uncertain_capture_site", flopName(capture), procOf(capture).loc});
                }
            }
            for (auto di : qualifiedCaptures)
                d.evidence.push_back({"qualified_payload_capture", flopName(m->flops[di]) + ": data capture requires a recognised destination-domain synchronised control", procOf(m->flops[di]).loc});
            if (!unsafeCaptures.empty() && (!uncertainCaptures.empty() || !qualifiedCaptures.empty()))
                d.evidence.push_back({"mixed_capture_results", "an established unsafe capture remains an ERROR; other captures retain their separate unresolved or qualified status", {}});
            d.nextStep = !qualifiedCaptures.empty() ? "Review the source-side payload stability and timing obligation (CDC-039); keep every asynchronous control input synchronised."
                         : declaredSync ? "Review the declared input timing contract against the external source; the declaration is trusted, not independently proved."
                         : allChainsRecognised ? "Review chain depth, source stability and pulse duration and the synchroniser's timing assumptions."
                         : inRel == Relation::Unknown ? "Declare the actual input clock intent (set_input_delay -clock) and review whether asynchronous-input synchronisation is needed."
                         : "Review a suitable receiving-domain input synchroniser or justify a waiver; a clock declaration does not repair this crossing.";
            for (auto qualified : qualifiedCaptures) {
                Diagnostic obligation = mk("CDC-039", Severity::Warning,
                    "primary-input payload " + inFull + " is captured under a recognised synchronised qualifier",
                    procOf(m->flops[qualified]).loc, procOf(m->flops[qualified]).instancePath, Confidence::Heuristic);
                obligation.signals = {inFull};
                obligation.crossing = inputCrossing;
                const auto& qualifiedFlop = m->flops[qualified];
                obligation.crossing->capture = endpoint(qualifiedFlop, stripBit(flopName(qualifiedFlop)), dom);
                obligation.crossing->widthBits = 1;
                obligation.proofState = ProofState::NotAttempted;
                obligation.evidence.push_back({"proof_obligation", "payload stability from before qualifier launch until after destination capture, plus source timing and protocol correctness, are not proven", {}});
                obligation.nextStep = "Hold the payload stable until the destination acknowledges, or establish an equivalent protocol and timing bound.";
                add(std::move(obligation));
                evaluated["CDC-039"]++;
            }
            for (auto unresolvedDi : undecidedQualifiers) {
                const auto incoherentIt = incoherentInputBundles.find({nameId(inNode), stripBit(flopName(m->flops[unresolvedDi])), dom});
                const std::string detailReason = incoherentIt != incoherentInputBundles.end() ? (": " + incoherentIt->second) : "";
                Diagnostic unresolved = mk("CDC-039", Severity::Warning,
                    "payload qualification is unresolved for primary input " + inFull + " -> " + m->domains[dom].name + detailReason,
                    procOf(m->flops[unresolvedDi]).loc,
                    procOf(m->flops[unresolvedDi]).instancePath, Confidence::Unresolved);
                unresolved.signals = {inFull};
                unresolved.crossing = inputCrossing;
                unresolved.crossing->capture = endpoint(m->flops[unresolvedDi], stripBit(flopName(m->flops[unresolvedDi])), dom);
                unresolved.crossing->widthBits = 1;
                unresolved.proofState = ProofState::NotAttempted;
                unresolved.evidence.push_back({"proof_obligation", "establish that every data-moving capture requires a synchronised control from the declared input launch domain; then establish source stability and timing" + detailReason, procOf(m->flops[unresolvedDi]).loc});
                unresolved.nextStep = "Review the capture predicate and synchronizer source relationship; establish the payload stability protocol separately.";
                add(std::move(unresolved));
                evaluated["CDC-039"]++;
                incomplete("payload-qualification", inFull + " -> " + m->domains[dom].name + ": qualifier necessity is unresolved" + detailReason);
            }
            if (!uncertainCaptures.empty() && inRel == Relation::Async) {
                d.evidence.push_back({"proof_obligation", "stage mismatches, opaque calls, conservative graph precision, or unresolved qualification prevent establishing containment", {}});
                incomplete("graph-precision", "CDC-029 input-to-capture dependency unresolved for '" + inFull + "'");
            }
            if (establishedUnsafeSample) for (auto di : uncertainCaptures) {
                const auto& unresolvedFlop = m->flops[di];
                Diagnostic unresolved = mk("CDC-029", Severity::Warning,
                    "capture " + flopName(unresolvedFlop) + " of asynchronous input '" + inFull + "' remains Unresolved: dependency or qualification is not established",
                    procOf(unresolvedFlop).loc, procOf(unresolvedFlop).instancePath, Confidence::Unresolved);
                unresolved.identityKey = inputIdentity + ":unresolved:" + flopName(unresolvedFlop);
                unresolved.signals = {inFull, flopName(unresolvedFlop)};
                unresolved.crossing = inputCrossing;
                unresolved.crossing->capture = endpoint(unresolvedFlop, stripBit(flopName(unresolvedFlop)), dom);
                unresolved.crossing->widthBits = 1;
                unresolved.proofState = ProofState::NotAttempted;
                unresolved.evidence.push_back({"input_delay_contract", contract, decl ? decl->loc : SourceLocation{}});
                unresolved.evidence.push_back({"capture_register", flopName(unresolvedFlop), procOf(unresolvedFlop).loc});
                unresolved.evidence.push_back({"proof_obligation", "opaque calls, conservative graph precision, or unresolved qualification cannot establish unsafe sampling", procOf(unresolvedFlop).loc});
                unresolved.nextStep = "Review this capture's actual data/control dependency and qualification; resolve the path before treating it as safe.";
                add(std::move(unresolved));
            }
            // the synchroniser structure checks apply to primary inputs exactly as to register crossings
            if (!declaredSync) {
                for (auto di : shortCaptures) {
                    const auto& capture = m->flops[di];
                    Diagnostic shortChain = mk("CDC-005",
                        inRel == Relation::Async ? Severity::Error : inRel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                        "input synchroniser for '" + inFull + "' has depth " + std::to_string(chains[di].depth) +
                        ", below the required " + std::to_string(minStages),
                        procOf(capture).loc, procOf(capture).instancePath,
                        inRel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
                    shortChain.identityKey = inputIdentity + ":" + flopName(capture);
                    shortChain.crossing = inputCrossing;
                    shortChain.crossing->capture = endpoint(capture, stripBit(flopName(capture)), dom);
                    shortChain.crossing->widthBits = 1;
                    shortChain.signals = {inFull, flopName(capture)};
                    if (inRel == Relation::Exclusive)
                        shortChain.evidence.push_back({"trust_model", "the launch and capture clocks are declared exclusive by constraint (trusted, not verified)", {}});
                    shortChain.nextStep = "Add stages or lower CDC-005.min_stages if the library's flops justify it.";
                    add(std::move(shortChain));
                    evaluated["CDC-005"]++;
                }
                int fan = 0, link = 0, mid = 0;
                for (auto di : dsts) if (chains[di].depth >= 2 || chains[di].linkLogic) { fan += chains[di].extraFanout; link += chains[di].linkLogic; mid += chains[di].midFanout; }
                auto sx = [&](const char* rule, std::string msg, Severity severity, Confidence confidence) {
                    Diagnostic x = mk(rule, severity, std::move(msg), procOf(f0).loc, procOf(f0).instancePath, confidence);
                    x.identityKey = inputIdentity;
                    x.crossing = inputCrossing;
                    x.signals = {inFull};
                    if (severity == Severity::Info && inRel == Relation::Exclusive)
                        x.evidence.push_back({"trust_model", "the launch and capture clocks are declared exclusive by constraint (trusted, not verified)", {}});
                    for (auto di : dsts) {
                        for (const auto& target : chains[di].fanoutTargets)
                            x.evidence.push_back({"downstream_tap", target.name, target.loc});
                        for (auto stage : chains[di].stages) {
                            auto stageName = stripBit(flopName(m->flops[stage]));
                            x.evidence.push_back({"capture_stage", stageName, procOf(m->flops[stage]).loc});
                        }
                        if (chains[di].linkLogic && chains[di].linkTarget != kNoNode) {
                            auto targetIt = m->flopOfQ.find(chains[di].linkTarget);
                            if (targetIt != m->flopOfQ.end()) {
                                const Flop& targetFlop = m->flops[targetIt->second];
                                x.evidence.push_back({"capture_stage", stripBit(flopName(targetFlop)), procOf(targetFlop).loc});
                            }
                        }
                    }
                    evaluated[rule]++;
                    if (confidence == Confidence::Unresolved)
                        incomplete("clock-relationships", "unknown launch relationship for input '" + inFull +
                                   "' into '" + m->domains[dom].name + "'");
                    add(std::move(x));
                };
                const auto confidence = inRel == Relation::Unknown ? Confidence::Unresolved :
                                        inRel == Relation::Exclusive ? Confidence::Informational : Confidence::Established;
                const auto hazardSeverity = inRel == Relation::Exclusive ? Severity::Info : Severity::Warning;
                const auto stageSeverity = inRel == Relation::Async ? Severity::Error : hazardSeverity;
                if (fan) sx("CDC-007", "first synchroniser stage of input '" + inFull + "' also feeds logic other than the next stage", stageSeverity, confidence);
                if (mid) sx("CDC-007", "an intermediate synchroniser stage of input '" + inFull + "' also feeds logic other than the next stage", stageSeverity, confidence);
                if (link) sx("CDC-006", "combinational logic between the synchroniser stages of input '" + inFull + "'", stageSeverity, confidence);
                if (inputLogic_.count({inNode, dom}))
                    sx("CDC-008", "combinational logic between primary input '" + inFull + "' and its capturing register: glitches may be sampled", hazardSeverity, confidence);
                bool merged = false;
                for (auto di : dsts) merged = merged || multiInputDst_.count(di);
                if (merged && inputReconvergenceReported.emplace(inFull, dom).second)
                    sx("CDC-010", "primary input '" + inFull + "' is combined with other asynchronous inputs before it is sampled", Severity::Warning, Confidence::Heuristic);
                const auto bits = inputBitsByNameDomain.find(uint64_t{dom} << 32 | nameId(inNode));
                if (bits != inputBitsByNameDomain.end() && bits->second.bits >= 2) {
                    const auto& bus = bits->second;
                    const Relation busRel = bus.relationship();
                    const bool async = busRel == Relation::Async;
                    const auto representative = async ? bus.firstAsync : bus.first;
                    if (inNode == representative) {
                        const auto width = async ? bus.asyncBits : bus.bits;
                        Diagnostic busFinding = mk("CDC-009", busRel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                            std::to_string(width) + " bits of primary input '" + inFull + "' are synchronised independently: bit skew can capture an inconsistent value",
                            procOf(f0).loc, procOf(f0).instancePath, busRel == Relation::Unknown ? Confidence::Unresolved :
                            busRel == Relation::Exclusive ? Confidence::Informational : Confidence::Established);
                        busFinding.identityKey = inFull + ":" + m->domains[dom].name + ":" + relName(busRel);
                        busFinding.crossing = inputCrossing;
                        busFinding.crossing->relationship = relationshipName(busRel);
                        if (busRel == Relation::Unknown) {
                            busFinding.crossing->launch.sdcClock.clear();
                            incomplete("clock-relationships", "unknown or mixed launch relationships for input bus '" + inFull +
                                       "' into '" + m->domains[dom].name + "'");
                        }
                        busFinding.crossing->widthBits = width;
                        busFinding.signals = {inFull};
                        busFinding.proofState = ProofState::NotAttempted;
                        if (busRel == Relation::Exclusive)
                            busFinding.evidence.push_back({"trust_model", "the launch and capture clocks are declared exclusive by constraint (trusted, not verified)", {}});
                        evaluated["CDC-009"] += width;
                        add(std::move(busFinding));
                    }
                }
            }
            const bool allLaunchesSameOrExclusive = !matchingDecls.empty() && !rejectedMode && !hasUnknownMode &&
                std::all_of(matchingDecls.begin(), matchingDecls.end(), [&](const auto& active) {
                    const auto rel = rel_->relationToName(dom, active.delay->clock);
                    return rel == Relation::Same || rel == Relation::Exclusive;
                });
            // Evaluate chains in a stable name order and retain a bounded set of
            // positive siblings. One primary-input/domain finding carries the
            // evidence for all of them; duplicate findings add no information.
            std::vector<uint32_t> orderedDsts = dsts;
            std::sort(orderedDsts.begin(), orderedDsts.end(), [&](uint32_t a, uint32_t b) {
                return flopName(m->flops[chains[a].stages.front()]) < flopName(m->flops[chains[b].stages.front()]);
            });
            orderedDsts.erase(std::unique(orderedDsts.begin(), orderedDsts.end()), orderedDsts.end());
            std::vector<uint32_t> positiveChains;
            std::map<uint32_t, std::vector<std::string>> positiveWordEvidence;
            size_t positiveChainCount = 0;
            size_t positiveWordMappingCount = 0;
            bool positiveChainScanCapped = false;
            constexpr size_t maxPositiveChainEvidence = 16;
            const bool allAtLeastTwoStages = std::all_of(dsts.begin(), dsts.end(), [&](uint32_t di) {
                const auto& chain = chains[di];
                return !chain.dualEdgeStage && !chain.callEffectsUnknown && !chain.linkLogic &&
                       !chain.feedbackWithFanout && chain.depth >= 2;
            });
            const bool cdc044Eligible = allAtLeastTwoStages && !allLaunchesSameOrExclusive;
            bool adjacentSearchIncomplete = false;
            for (auto di : orderedDsts) {
                if (!cdc044Eligible) break;
                findAdjacentStageUses(chains[di]);
                adjacentSearchIncomplete = adjacentSearchIncomplete || chains[di].adjacentStageSearchIncomplete;
                std::vector<std::string> wordBits;
                size_t wordCount = 0;
                const bool word = observableShiftWord(chains[di], positiveChainCount < maxPositiveChainEvidence ? &wordBits : nullptr, &wordCount);
                const bool positive = chains[di].adjacentStageUse || word;
                if (!positive) continue;
                ++positiveChainCount;
                positiveWordMappingCount += wordCount;
                if (positiveChains.size() < maxPositiveChainEvidence) {
                    positiveChains.push_back(di);
                    if (word) positiveWordEvidence.emplace(di, std::move(wordBits));
                } else {
                    // One extra positive chain proves the evidence cap was
                    // crossed. Do not keep counting exact omissions.
                    positiveChainScanCapped = true;
                    break;
                }
            }
            const bool streamEvidence = positiveChainCount != 0;
            if (allAtLeastTwoStages && streamEvidence && !allLaunchesSameOrExclusive) {
                evaluated["CDC-044"]++;
                const uint32_t triggerDi = positiveChains.front();
                const auto& triggerChain = chains[triggerDi];
                const auto& triggerFlop = m->flops[triggerChain.stages.front()];
                Diagnostic st = mk("CDC-044", Severity::Info, "synchronised input '" + inFull + "' has adjacent-stage data/control use or an observable parallel shift word: review asynchronous event/serial use",
                                   procOf(triggerFlop).loc, procOf(triggerFlop).instancePath, Confidence::Heuristic);
                st.identityKey = inputIdentity;
                st.crossing = inputCrossing;
                st.signals = {inFull};
                st.proofState = ProofState::NotAttempted;
                st.evidence.push_back({"triggering_capture", flopName(triggerFlop), procOf(triggerFlop).loc});
                size_t adjacentShown = 0;
                size_t adjacentOmitted = 0;
                size_t wordShown = 0;
                size_t wordOmitted = positiveWordMappingCount;
                constexpr size_t maxAdjacentEvidence = 16;
                constexpr size_t maxWordEvidence = 16;
                for (auto di : positiveChains) {
                    const auto& chain = chains[di];
                    std::string chainText;
                    for (auto stage : chain.stages) {
                        if (!chainText.empty()) chainText += " -> ";
                        chainText += flopName(m->flops[stage]);
                    }
                    st.evidence.push_back({"capture_stage_chain", chainText, {}});
                    for (const auto& [first, second, target] : chain.adjacentStageUses) {
                        if (adjacentShown >= maxAdjacentEvidence) { ++adjacentOmitted; continue; }
                        const auto a = flopName(m->flops[chain.stages[first]]);
                        const auto b = flopName(m->flops[chain.stages[second]]);
                        st.evidence.push_back({"adjacent_stage_use", a + " + " + b + " -> " + m->nameOf(target), {}});
                        ++adjacentShown;
                    }
                    adjacentOmitted += chain.adjacentStageUseCount - chain.adjacentStageUses.size();
                    if (chain.stages.size() == 17)
                        st.evidence.push_back({"analysis_limit", "stage evidence covers at most 17 modeled stages per chain; further stages may be unexamined", {}});
                    if (auto word = positiveWordEvidence.find(di); word != positiveWordEvidence.end())
                        for (const auto& mapping : word->second) {
                            if (wordShown >= maxWordEvidence) continue;
                            st.evidence.push_back({"parallel_shift_word_bit", mapping, {}});
                            ++wordShown;
                            --wordOmitted;
                        }
                }
                if (positiveChainScanCapped)
                    st.evidence.push_back({"positive_chain_disclosure", "at least 17 positive sibling chains; chain, consumer, and mapping totals beyond the 16-chain evidence cap were not analyzed", {}});
                else if (positiveChainCount > positiveChains.size())
                    st.evidence.push_back({"positive_chain_disclosure", std::to_string(positiveChainCount - positiveChains.size()) + " additional positive sibling chains omitted (chain evidence capped at 16)", {}});
                if (adjacentOmitted)
                    st.evidence.push_back({"adjacent_stage_use_disclosure", std::to_string(adjacentOmitted) + " additional consumers omitted (consumer evidence capped at 16)", {}});
                if (positiveChainScanCapped && positiveWordMappingCount > 16)
                    st.evidence.push_back({"parallel_shift_word_disclosure", "at least " + std::to_string(positiveWordMappingCount - 16) + " additional parallel shift-word mappings are known beyond the 16-entry evidence cap; more may be unexamined", {}});
                else if (positiveChainScanCapped)
                    st.evidence.push_back({"parallel_shift_word_disclosure", "mapping count may be incomplete because sibling analysis stopped after the chain evidence cap", {}});
                else if (wordOmitted)
                    st.evidence.push_back({"parallel_shift_word_disclosure", std::to_string(wordOmitted) + " additional mappings omitted (mapping evidence capped at 16)", {}});
                st.evidence.push_back({"proof_obligation", "review event/pulse spacing and required delivery against the destination sampler; no rate or spacing is inferred from chain depth and neither period is established here", {}});
                add(std::move(st));
            }
            if (cdc044Eligible && adjacentSearchIncomplete) {
                Diagnostic unresolved = mk("CDC-044", Severity::Warning,
                    "CDC-044 adjacent-stage analysis for synchronised input '" + inFull +
                    "' exhausted its shared graph-work budget; a positive witness may be reported, but remaining chains are Unresolved",
                    procOf(m->flops[orderedDsts.front()]).loc, procOf(m->flops[orderedDsts.front()]).instancePath,
                    Confidence::Unresolved);
                unresolved.identityKey = inputIdentity + ":adjacent-search-budget";
                unresolved.crossing = inputCrossing;
                unresolved.signals = {inFull};
                unresolved.evidence.push_back({"analysis_limit", "adjacent-stage search stopped after the shared one-million-node work budget; no negative conclusion is made for unsearched cones", {}});
                add(std::move(unresolved));
                incomplete("graph-precision", "CDC-044 adjacent-stage search budget exhausted for '" + inFull + "'");
            }
            add(std::move(d));
        }

        reportUnresolvedCaptures(cap);

        analyseStructure();

        // ---- CST-004: unknown relationships
        evaluated["CST-004"] += unknownPairs.size();
        auto addClockSignals = [&](uint32_t dom, std::vector<std::string>& sigs) {
            auto addUnique = [&](std::string s) {
                if (!s.empty() && std::find(sigs.begin(), sigs.end(), s) == sigs.end())
                    sigs.push_back(std::move(s));
            };
            const std::string rootName = stripBit(m->domains[dom].name);
            addUnique(rootName);
            const std::string sdcName = domainClockName(dom);
            if (!sdcName.empty() && sdcName != rootName) {
                const auto dot = rootName.rfind('.');
                const std::string rootLeaf = dot == std::string::npos ? rootName : rootName.substr(dot + 1);
                if (sdcName != rootLeaf) addUnique(sdcName);
            }
        };
        for (auto& [a, b] : unknownPairs) {
            Diagnostic d = mk("CST-004", Severity::Warning,
                              "relationship between clock '" + m->domains[a].name + "' and '" + m->domains[b].name + "' is UNKNOWN; " +
                              std::to_string(unknownPairCount[{a, b}]) + " register-bit crossing(s) between them are unresolved, not safe",
                              {}, {}, Confidence::Unresolved);
            addClockSignals(a, d.signals);
            addClockSignals(b, d.signals);
            d.nextStep = "Declare the relationship (create_clock + set_clock_groups -asynchronous, or a common clock) with --sdc.";
            add(std::move(d));
            incomplete("clock-relationships", "unknown relationship '" + m->domains[a].name + "' <-> '" + m->domains[b].name + "'");
        }
        if (unresolvedEnds) {
            Diagnostic d = mk("CST-004", Severity::Warning,
                              std::to_string(unresolvedEnds) + " capture path(s) involve a register with an unidentified clock; crossing status is unresolved",
                              {}, {}, Confidence::Unresolved);
            d.signals.assign(unresolvedClockSignals.begin(), unresolvedClockSignals.end());
            add(std::move(d));
            incomplete("clock-relationships", std::to_string(unresolvedEnds) + " path(s) with an unidentified clock");
        }

        // These counters are optional and live only in --timings. Vector capacities are actual; tree/map bytes are an estimate
        // that excludes allocator metadata and implementation-specific node padding.
        if (cfg->timings) {
            uint64_t groupedSources = 0, groupedCaptures = 0, largestGroupSources = 0, largestGroupCaptures = 0;
            uint64_t groupContainerBytes = 0;
            for (const auto& [key, g] : groups) {
                groupedSources += g.srcBits.size();
                groupedCaptures += g.dstFlops.size();
                largestGroupSources = std::max<uint64_t>(largestGroupSources, g.srcBits.size());
                largestGroupCaptures = std::max<uint64_t>(largestGroupCaptures, g.dstFlops.size());
                groupContainerBytes += sizeof(key) + 3 * sizeof(void*) + key.first.capacity() + key.second.capacity() +
                                       sizeof(Group) + g.dstFlops.capacity() * sizeof(uint32_t) +
                                       g.srcBits.size() * (sizeof(NodeId) + 3 * sizeof(void*)) +
                                       g.srcName.capacity() + g.dstName.capacity() + g.hier.capacity();
            }
            uint64_t inputCaptureMemberships = 0, largestInputList = 0;
            uint64_t inputContainerBytes = 0;
            for (const auto& [key, destinations] : inputsToDomain) {
                inputCaptureMemberships += destinations.size();
                largestInputList = std::max<uint64_t>(largestInputList, destinations.size());
                inputContainerBytes += sizeof(key) + sizeof(destinations) + 3 * sizeof(void*) +
                                       destinations.capacity() * sizeof(uint32_t);
            }
            uint64_t nameMapContainerBytes = nameIds.size() * (sizeof(std::string) + sizeof(uint32_t) + 3 * sizeof(void*));
            for (const auto& entry : nameIds) if (entry.first.capacity() > 15) nameMapContainerBytes += entry.first.capacity() + 1;
            const uint64_t pairLookupContainerBytes = groupOf.bucket_count() * sizeof(void*) +
                                                      groupOf.size() * (sizeof(uint64_t) + sizeof(decltype(groupOf)::mapped_type) + 3 * sizeof(void*));
            cfg->timings->checkpoint({
                {"cdc.crossings.group_count", groups.size()},
                {"cdc.crossings.group_source_memberships", groupedSources},
                {"cdc.crossings.group_capture_memberships", groupedCaptures},
                {"cdc.crossings.group_max_source_members", largestGroupSources},
                {"cdc.crossings.group_max_capture_members", largestGroupCaptures},
                {"cdc.crossings.group_container_estimate_bytes", groupContainerBytes},
                {"cdc.crossings.input_domain_key_count", inputsToDomain.size()},
                {"cdc.crossings.input_capture_memberships", inputCaptureMemberships},
                {"cdc.crossings.input_max_capture_list", largestInputList},
                {"cdc.crossings.input_container_estimate_bytes", inputContainerBytes},
                {"cdc.crossings.name_id_count", nameIds.size()},
                {"cdc.crossings.name_map_container_estimate_bytes", nameMapContainerBytes},
                {"cdc.crossings.source_destination_group_lookup_count", groupOf.size()},
                {"cdc.crossings.source_destination_lookup_estimate_bytes", pairLookupContainerBytes},
            });
        }
    }
};
} // namespace cdc
} // namespace sverity
