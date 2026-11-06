#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/symbols/ValueSymbol.h>
#include <slang/ast/types/Type.h>
#include "sverity/analyzer.h"
#include "sverity/json.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

#include "sverity/waiver.h"
#include "domains/model.h"
#include "domains/relations.h"

namespace sverity {

namespace {

// The objects written by anything but a continuous assignment or port connection: a procedure in any form, a subroutine, a force, a
// declaration initialiser, a primitive or a net alias. A case value may only stand in for no driver at all, or agree with one continuous
// driver, so none of these can take one. Only the written roots of a target count: a select's index, a call's input argument
// and a displayed value are reads. A target form not listed below falls back to every name in it, which only rejects a constraint.
std::set<const slang::ast::Symbol*> overriddenSymbols(const slang::ast::RootSymbol& root) {
    using namespace slang::ast;
    struct Names : ASTVisitor<Names, VisitFlags::AllGood> {
        std::set<const Symbol*>& found;
        explicit Names(std::set<const Symbol*>& out) : found(out) {}
        void add(const Symbol& symbol) {
            found.insert(&symbol);
            if (symbol.kind == SymbolKind::ModportPort && symbol.as<ModportPortSymbol>().internalSymbol) found.insert(symbol.as<ModportPortSymbol>().internalSymbol);
        }
        void handle(const NamedValueExpression& name) { add(name.symbol); }
        void handle(const HierarchicalValueExpression& name) { add(name.symbol); }
    };
    struct Writes : ASTVisitor<Writes, VisitFlags::AllGood> {
        std::set<const Symbol*> found;
        bool procedural = false;
        void record(const Expression* target) {
            if (!target) return;
            switch (target->kind) {
                case ExpressionKind::NamedValue:
                case ExpressionKind::HierarchicalValue: {
                    Names names(found);
                    target->visit(names);
                    return;
                }
                case ExpressionKind::ElementSelect: record(&target->as<ElementSelectExpression>().value()); return;
                case ExpressionKind::RangeSelect: record(&target->as<RangeSelectExpression>().value()); return;
                case ExpressionKind::MemberAccess: record(&target->as<MemberAccessExpression>().value()); return;
                case ExpressionKind::Concatenation:
                    for (auto* operand : target->as<ConcatenationExpression>().operands()) record(operand);
                    return;
                case ExpressionKind::Streaming:
                    for (const auto& stream : target->as<StreamingConcatenationExpression>().streams()) record(stream.operand);
                    return;
                default: {   // assignment patterns, conversions and anything else: every name in it
                    Names names(found);
                    target->visit(names);
                }
            }
        }
        void handle(const VariableSymbol& variable) {
            if (variable.getInitializer()) found.insert(&variable);
            visitDefault(variable);
        }
        void handle(const ProceduralBlockSymbol& block) {
            const bool outer = procedural;
            procedural = true;
            visitDefault(block);
            procedural = outer;
        }
        void handle(const SubroutineSymbol& subroutine) {
            const bool outer = procedural;
            procedural = true;
            visitDefault(subroutine);
            procedural = outer;
        }
        void handle(const AssignmentExpression& assignment) {
            if (procedural) record(&assignment.left());
            visitDefault(assignment);
        }
        void handle(const UnaryExpression& unary) {
            const bool step = unary.op == UnaryOperator::Preincrement || unary.op == UnaryOperator::Predecrement ||
                              unary.op == UnaryOperator::Postincrement || unary.op == UnaryOperator::Postdecrement;
            if (procedural && step) record(&unary.operand());
            visitDefault(unary);
        }
        // Output and inout arguments, including those of system tasks such as $readmemh, are bound as assignments and are
        // recorded there. A ref argument of a user subroutine is not, so it is recorded here; inputs are reads.
        void handle(const CallExpression& call) {
            if (procedural && !call.isSystemCall()) {
                const auto* subroutine = std::get<0>(call.subroutine);
                const auto formals = subroutine->getArguments();
                const auto actuals = call.arguments();
                for (size_t i = 0; i < actuals.size(); ++i) {
                    const bool constRef = i < formals.size() && formals[i]->flags.has(VariableFlags::Const);
                    if (i >= formals.size() || (formals[i]->direction == ArgumentDirection::Ref && !constRef)) record(actuals[i]);
                }
            }
            visitDefault(call);
        }
        void handle(const ProceduralAssignStatement& statement) {
            if (statement.assignment.kind == ExpressionKind::Assignment) record(&statement.assignment.as<AssignmentExpression>().left());
            visitDefault(statement);
        }
        void handle(const PrimitiveInstanceSymbol& primitive) {
            for (auto* connection : primitive.getPortConnections()) record(connection);
            visitDefault(primitive);
        }
        void handle(const NetAliasSymbol& alias) {
            for (auto* net : alias.getNetReferences()) record(net);
            visitDefault(alias);
        }
    } writes;
    root.visit(writes);
    return std::move(writes.found);
}

} // namespace

std::string currentDateIso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

Report analyze(const AnalyzerOptions& opts) {
    Report report;
    report.setToolInfo(SVERITY_VERSION, slangVersionString(), Catalog::embedded().version());
    report.setPathRoot(opts.pathRoot);
    {
        json::Value args = json::Value::array();
        for (auto& a : opts.frontend.slangArgs) {
            std::string prefix, path = a;
            if (a.size() > 2 && (a.rfind("-I/", 0) == 0 || a.rfind("-y/", 0) == 0 || a.rfind("-v/", 0) == 0 || a.rfind("-f/", 0) == 0)) {
                prefix = a.substr(0, 2); path = a.substr(2);
            }
            if (!opts.pathRoot.empty() && std::filesystem::path(path).is_absolute()) {
                auto relative = std::filesystem::path(path).lexically_normal().lexically_relative(opts.pathRoot);
                if (!relative.empty()) path = relative.generic_string();
            }
            args.push(prefix + path);
        }
        report.addInput("slang_args", args);
        report.addInput("source_waivers", opts.sourceWaivers);
        report.addInput("source_waiver_scope", "loaded design, include and library buffers only; unloaded library candidates are not read");
        report.addInput("profile", opts.config.profile);
        if (!opts.config.deck.empty()) report.addInput("deck", opts.config.deck);
        json::Value goals = json::Value::array();
        for (const auto& goal : opts.config.goals) goals.push(goal);
        report.addInput("goals", goals);
        report.addInput("goal_broadened", opts.config.goalBroadened);
        json::Value dis = json::Value::array();
        for (auto& r : opts.config.disabledRules) dis.push(r);
        report.addInput("disabled_rules", dis);
        json::Value options = json::Value::object();
        auto effective = opts.config.ruleOptions;
        effective.try_emplace("CDC-005.min_stages", "2");
        effective.try_emplace("RDC-006.min_stages", "2");
        effective.try_emplace("LNT-025.priority_chains", "info");
        effective.try_emplace("LNT-036.report_modular", "false");
        effective.try_emplace("LNT-006.dynamic_index", "false");
        effective.try_emplace("LNT-037.dynamic_divisor", "false");
        effective.try_emplace("LNT-061.allow", "");
        for (auto& [key, value] : effective) options.set(key, value);
        report.addInput("rule_options", options);
        json::Value overrides = json::Value::object();
        for (auto& [key, value] : opts.config.severityOverrides) overrides.set(key, toString(value));
        report.addInput("severity_overrides", overrides);
        report.addInput("upstream_warnings", opts.frontend.enableAllUpstreamWarnings ? "everything" : "slang-default");
    }
    // The elaborated design lives as long as the rules need it.
    std::unique_ptr<Design> design;
    Timings timings;
    const bool measuring = !opts.timingsFile.empty();
    const auto loadStart = Timings::Clock::now();
    try {
        design = loadDesign(opts.frontend, report);
    } catch (const std::exception& e) {
        Diagnostic d;
        d.ruleId = "LNT-001";
        d.severity = Severity::Fatal;
        d.message = std::string("internal failure while loading the design: ") + e.what();
        report.setExecutionError(d.message);
        report.add(std::move(d));
        report.markIncomplete("elaboration", "internal failure while loading the design");
    }
    if (design) {
        for (auto& f : opts.libraryFiles) {
            std::string text;
            auto fail = [&](const std::string& why) {
                Diagnostic d;
                d.ruleId = "CST-018";
                d.severity = Severity::Error;
                d.message = "library file '" + std::filesystem::path(f).filename().string() + "' " + why;
                d.nextStep = "Fix the library file; its cells are treated as ordinary modules (no synchroniser is recognised through them).";
                report.add(std::move(d));
                report.markIncomplete("constraints", "library file '" + std::filesystem::path(f).filename().string() + "' " + why);
            };
            if (!readFile(f, text)) { fail("could not be read"); continue; }
            auto pr = json::parse(text);
            if (!pr.ok) { fail("is not valid JSON (" + pr.error + ")"); continue; }
            for (auto& c : pr.value["cells"].asArray()) {
                LibCell lc;
                lc.name = c["name"].asString();
                if (c.find("kind")) lc.kind = c["kind"].asString();
                lc.clock = c["clock"].asString();
                lc.input = c["input"].asString();
                lc.output = c["output"].asString();
                lc.enable = c["enable"].asString();
                lc.reset = c["reset"].asString();
                lc.writeClock = c["write_clock"].asString();
                lc.readClock = c["read_clock"].asString();
                if (c.find("stages")) lc.stages = static_cast<int>(c["stages"].asInt(2));
                if (lc.kind == "flop") lc.stages = 1;
                if (c.find("edge")) lc.edge = c["edge"].asString() == "negedge" ? 2 : 1;
                if (c.find("reset_active")) lc.resetActiveLow = c["reset_active"].asString() != "high";
                if (c.find("reset_value")) lc.resetValue = c["reset_value"].asInt(0) ? '1' : '0';
                if (c.find("enable_latched")) lc.enableLatched = c["enable_latched"].asBool(false);
                if (c.find("sync_read")) lc.syncRead = c["sync_read"].asBool(true);
                if (c.find("ports"))
                    for (auto& p : c["ports"].asArray()) lc.ports.push_back({p["name"].asString(), p["dir"].asString(), p["role"].asString(), p["clock"].asString()});
                bool ok = !lc.name.empty() && lc.stages >= 1;
                if (lc.kind == "synchronizer" || lc.kind == "flop") ok = ok && !lc.clock.empty() && !lc.input.empty() && !lc.output.empty();
                else if (lc.kind == "clock_gate") ok = ok && !lc.clock.empty() && !lc.enable.empty() && !lc.output.empty();
                else if (lc.kind == "ram") ok = ok && !lc.writeClock.empty() && !lc.readClock.empty() && !lc.ports.empty();
                else if (lc.kind == "blackbox") ok = ok && !lc.ports.empty();
                else ok = false;
                for (auto& p : lc.ports)
                    ok = ok && !p.name.empty() && (p.dir == "in" || p.dir == "out" || p.dir == "inout");
                if (!ok) {
                    fail("has a cell entry that is incomplete or of an unsupported kind (kinds: synchronizer, flop, clock_gate, ram, blackbox; see LibCell in sverity/frontend.h)");
                    continue;
                }
                design->libCells[lc.name] = lc;
            }
        }
    }
    Config config = opts.config;
    auto reportPath = [&](const std::string& path) {
        auto absolute = std::filesystem::absolute(path).lexically_normal();
        auto root = opts.pathRoot.empty() ? std::filesystem::current_path() : std::filesystem::absolute(opts.pathRoot);
        return absolute.lexically_relative(root.lexically_normal()).generic_string();
    };
    for (auto& f : opts.sdcFiles) {
        report.addIdentityFile(std::filesystem::absolute(f).string());
        std::string text;
        config.constraintsProvided = true;
        if (!readFile(f, text)) {
            Diagnostic d;
            d.ruleId = "CST-018";
            d.severity = Severity::Error;
            d.identityKey = "unreadable-constraint";
            d.identityArguments = {std::filesystem::absolute(f).string()};
            d.message = "constraint file '" + std::filesystem::path(f).filename().string() + "' could not be read";
            d.nextStep = "Fix the path; no constraints from this file were applied.";
            report.add(std::move(d));
            report.markIncomplete("constraints", "constraint file '" + std::filesystem::path(f).filename().string() + "' unreadable");
            continue;
        }
        config.sdcFiles.push_back(reportPath(f));
        auto parsed = sdc::parse(text, std::filesystem::absolute(f).string());
        for (const auto& sourced : parsed.sourcedFiles) {
            config.sdcFiles.push_back(reportPath(sourced));
            report.addIdentityFile(std::filesystem::absolute(sourced).string());
        }
        config.constraints.merge(std::move(parsed));
    }
    json::Value sdcInputs = json::Value::array();
    for (const auto& file : config.sdcFiles) sdcInputs.push(file);
    report.addInput("sdc_files", sdcInputs);
    if (design && !config.constraints.caseAnalyses.empty()) {
        auto& baseline = model::getModel(*design);
        model::ClockRelations relations(baseline, config);
        const auto& cases = config.constraints.caseAnalyses;
        std::vector<std::set<model::NodeId>> targets(cases.size());
        std::map<model::NodeId, std::set<int>> values;
        design->caseResults.resize(cases.size());
        for (size_t i = 0; i < cases.size(); ++i) {
            for (const auto& ref : cases[i].objects) {
                const std::string kind = ref.kind.empty() ? "get_ports" : ref.kind;
                bool hit = false;
                for (model::NodeId n = 0; n < baseline.nodes.size(); ++n) {
                    const bool vector = baseline.symbolWidth(n) > 1;
                    const bool whole = relations.nameMatches(kind, ref.pattern, n, model::stripBit(baseline.nameOf(n)));
                    if (vector ? relations.bitMatches(kind, ref.pattern, n) : whole) {
                        hit = true; targets[i].insert(n); values[n].insert(cases[i].value);
                    } else if (vector && whole) {
                        // Tools differ on whether a bare bus name means its bits, and 1 on all bits is not the value 1.
                        hit = true;
                        design->caseResults[i].reason = "'" + ref.pattern + "' names a whole vector; name its bits, such as " + ref.pattern + "[0] or " + ref.pattern + "[*]";
                    }
                }
                if (!hit) design->caseResults[i].reason = "unknown object '" + ref.pattern + "'; entire declaration is NOT applied";
            }
        }
        // A clock/reset is not an ordinary Boolean data input. Retain its
        // original model until inactivity and asynchronous-event semantics exist.
        std::set<model::NodeId> eventInputs;
        std::set<model::NodeId> resetSources;
        auto ancestors = [&](auto&& self, model::NodeId n, bool clock, std::set<model::NodeId>& seen) -> void {
            if (n == model::kNoNode || !seen.insert(n).second) return;
            eventInputs.insert(n);
            for (auto ei : baseline.in[n]) {
                const auto& edge = baseline.edges[ei];
                if (edge.type == model::EdgeType::Comb && !(clock && edge.op == model::Op::MuxSel))
                    self(self, edge.from, clock, seen);
            }
        };
        for (const auto& flop : baseline.flops) {
            std::set<model::NodeId> seen;
            ancestors(ancestors, flop.clockSignal, true, seen);
            for (const auto& reset : flop.resets) {
                seen.clear(); ancestors(ancestors, reset.signal, false, seen);
                resetSources.insert(seen.begin(), seen.end());
            }
        }
        auto protectEvents = [&](const std::vector<model::NodeId>& nodes) {
            for (auto n : nodes) { std::set<model::NodeId> seen; ancestors(ancestors, n, false, seen); }
        };
        for (const auto& ram : baseline.rams) { protectEvents(ram.writeClk); protectEvents(ram.readClk); }
        for (const auto& gate : baseline.clockGates) { protectEvents(gate.clk); protectEvents(gate.enable); }
        for (const auto& instance : baseline.libInstances)
            for (const auto& port : instance.ports)
                if (port.role == model::PortRole::Clock) protectEvents(port.nodes);
                else if (port.role == model::PortRole::Reset)
                    for (auto n : port.nodes) { std::set<model::NodeId> seen; ancestors(ancestors, n, false, seen); resetSources.insert(seen.begin(), seen.end()); }
        const auto offsetIsIndex = [&](model::NodeId n) {
            const auto& type = baseline.nodes[n].sym->getType().getCanonicalType();
            if (!type.isSimpleBitVector() || baseline.nameOf(n).back() != ']') return false;
            const auto range = type.getFixedRange();
            return range.right == 0 && range.left > 0;
        };
        // Shared elaboration symbols across instances cannot hold one value, so only a symbol of one instance gets one.
        std::map<const slang::ast::ValueSymbol*, uint64_t> counts;
        for (model::NodeId n = 0; n < baseline.nodes.size(); ++n) ++counts[baseline.nodes[n].sym];
        // Nothing these write can be proved or assumed constant.
        const auto overridden = overriddenSymbols(design->compilation->getRoot());
        const auto isOverridden = [&](model::NodeId n) { return overridden.count(baseline.nodes[n].sym) != 0; };
        // Each assignment or port connection into the node is a driver of its own, including one from a child's output.
        const auto driverCount = [&](model::NodeId n) {
            std::set<uint32_t> procs;
            for (auto ei : baseline.in[n]) procs.insert(baseline.edges[ei].proc);
            if (auto it = baseline.constDrivers.find(n); it != baseline.constDrivers.end())
                for (const auto& driver : it->second) procs.insert(driver.proc);
            return procs.size();
        };
        const auto isDriven = [&](model::NodeId n) {
            return !baseline.in[n].empty() || baseline.constDrivers.count(n) || baseline.selfComb.count(n) ||
                   baseline.blackBoxDriven[n] || baseline.unresolvedDriven[n];
        };
        // A constraint can say what an undriven object holds. For a driven one it may only agree with what the drivers prove:
        // overriding them would let a mistaken constraint delete the defect being checked.
        std::vector<std::vector<model::NodeId>> toProve(cases.size());
        const auto plainObject = [&](model::NodeId n) {
            const auto* symbol = baseline.nodes[n].sym;
            if (!symbol->getType().getCanonicalType().isSimpleBitVector()) return false;
            for (const auto* scope = symbol->getParentScope(); scope; scope = scope->asSymbol().getParentScope())
                if (scope->asSymbol().kind == slang::ast::SymbolKind::InstanceBody)
                    return scope->asSymbol().as<slang::ast::InstanceBodySymbol>().getDefinition().definitionKind != slang::ast::DefinitionKind::Interface;
            return true;
        };
        for (size_t i = 0; i < cases.size(); ++i) {
            auto& result = design->caseResults[i];
            for (auto n : targets[i]) {
                const bool vector = baseline.symbolWidth(n) != 1;
                const auto drivers = baseline.booleanDrivers.find(n);
                if (values[n].size() > 1) { result.reason = "conflicting values on '" + baseline.nameOf(n) + "'; apply nothing"; break; }
                if (!result.reason.empty()) continue;
                if (!plainObject(n)) result.reason = "only a plain net or variable in a module is supported: a struct or an interface member is not followed";
                else if (vector && !offsetIsIndex(n)) result.reason = "only a vector declared [N:0] is supported, where a bit's offset is its index";
                else if (resetSources.count(n)) result.reason = "constant reset source not applied: reset level, initialization and release semantics are not modeled; reset obligations remain analyzed";
                else if (eventInputs.count(n) || relations.isDeclaredClock(n)) result.reason = "constant clock source not applied: clock inactivity and retained-state semantics are not modeled; asynchronous controls remain analyzed";
                else if (baseline.flopOfQ.count(n)) result.reason = "register output not applied: nothing shows it holds this value over time; initialization, updates and asynchronous controls remain analyzed";
                else if (isOverridden(n)) result.reason = "target is written procedurally, forced, initialised, or tied to a primitive or alias, so its value is not established";
                else if (!isDriven(n)) {
                    if (!baseline.topInput[n] && counts[baseline.nodes[n].sym] != baseline.symbolWidth(n)) result.reason = "the module has several instances, and a value on one must not reach the others";
                } else if (driverCount(n) > 1 || (drivers != baseline.booleanDrivers.end() && drivers->second.size() > 1) || baseline.selfComb.count(n) || baseline.blackBoxDriven[n] ||
                           baseline.unresolvedDriven[n] || baseline.uncertainCallDrivers.count(n)) {
                    result.reason = "target has multiple or unresolved drivers; its resolved value is not established";
                } else toProve[i].push_back(n);
            }
        }
        std::map<model::NodeId, int> known;
        std::map<const slang::ast::ValueSymbol*, int> derived;
        const auto evaluate = [&](auto&& self, const model::BoolFactPtr& fact) -> int {
            if (!fact || fact->opaque) return -1;
            using K = model::BoolFact::Kind;
            if (fact->kind == K::Constant) return fact->value;
            if (fact->kind == K::Node) { auto it = known.find(fact->node); return it == known.end() ? -1 : it->second; }
            if (fact->kind == K::Not && fact->args.size() == 1) {
                int v = self(self, fact->args[0]); return v < 0 ? -1 : !v;
            }
            if (fact->kind == K::And || fact->kind == K::Or) {
                bool unknown = false;
                for (const auto& arg : fact->args) {
                    int v = self(self, arg);
                    if (v < 0) unknown = true;
                    else if (v == (fact->kind == K::Or)) return v;
                }
                return unknown ? -1 : fact->kind == K::And;
            }
            return -1;
        };
        // Propagates only single unconditional scalar continuous drivers.
        const auto propagate = [&] {
            for (model::NodeId n = 0; n < baseline.nodes.size(); ++n) {
                auto it = design->caseConstants.find(baseline.nodes[n].sym);
                if (it != design->caseConstants.end()) known[n] = it->second;
                auto bits = design->caseBits.find(baseline.nodes[n].sym);
                if (bits != design->caseBits.end() && bits->second[baseline.nodes[n].bit] >= 0) known[n] = bits->second[baseline.nodes[n].bit];
            }
            bool changed = true;
            while (changed) {
                changed = false;
                for (const auto& [n, drivers] : baseline.booleanDrivers) {
                    if (known.count(n) || drivers.size() != 1 || !drivers[0].unconditional || driverCount(n) != 1 || isOverridden(n) ||
                        baseline.symbolWidth(n) != 1 || counts[baseline.nodes[n].sym] != 1 ||
                        baseline.flopOfQ.count(n) || baseline.selfComb.count(n) || baseline.blackBoxDriven[n] ||
                        baseline.unresolvedDriven[n] || baseline.uncertainCallDrivers.count(n)) continue;
                    bool continuous = !baseline.in[n].empty() || baseline.constDrivers.count(n);
                    auto validProc = [&](uint32_t proc) { auto k = baseline.procs[proc].kind;
                        return k == model::ProcKind::Continuous || k == model::ProcKind::PortConnection; };
                    for (auto ei : baseline.in[n]) continuous &= validProc(baseline.edges[ei].proc);
                    if (auto it = baseline.constDrivers.find(n); it != baseline.constDrivers.end())
                        for (const auto& driver : it->second) continuous &= validProc(driver.proc);
                    if (!continuous) continue;
                    int v = evaluate(evaluate, drivers[0].value);
                    if (v >= 0) { known[n] = v; derived[baseline.nodes[n].sym] = v; changed = true; }
                }
            }
        };
        const auto apply = [&](size_t i) {
            auto& result = design->caseResults[i];
            result.applied = true;
            for (auto n : targets[i]) {
                if (baseline.symbolWidth(n) == 1) design->caseConstants[baseline.nodes[n].sym] = cases[i].value;
                else {
                    auto& bits = design->caseBits[baseline.nodes[n].sym];
                    bits.resize(baseline.symbolWidth(n), -1);
                    bits[baseline.nodes[n].bit] = static_cast<int8_t>(cases[i].value);
                }
                result.names.push_back(baseline.nameOf(n));
                result.assumed = result.assumed || (!baseline.topInput[n] && !isDriven(n));
            }
        };
        for (size_t i = 0; i < cases.size(); ++i)
            if (design->caseResults[i].reason.empty() && !targets[i].empty() && toProve[i].empty()) apply(i);
        propagate();
        // Each declaration applied can prove the next, so repeat until nothing changes.
        for (bool progress = true; progress;) {
            progress = false;
            for (size_t i = 0; i < cases.size(); ++i) {
                auto& result = design->caseResults[i];
                if (result.applied || !result.reason.empty() || toProve[i].empty()) continue;
                bool proved = true;
                for (auto n : toProve[i]) {
                    const auto value = known.find(n);
                    proved = proved && value != known.end() && value->second == cases[i].value;
                    if (value != known.end() && value->second != cases[i].value)
                        result.reason = "requested value contradicts a proven RTL driver of '" + baseline.nameOf(n) + "'; declaration not applied";
                }
                if (proved) { apply(i); propagate(); progress = true; }
            }
        }
        for (size_t i = 0; i < cases.size(); ++i) {
            auto& result = design->caseResults[i];
            if (!result.applied && result.reason.empty() && !toProve[i].empty())
                result.reason = "driven target is not proved constant in this mode; driver override is not implemented";
        }
        if (design->hasCaseMode()) {
            for (const auto& [symbol, value] : derived) design->caseConstants.emplace(symbol, value);
            design->model.reset();
        }
    }
    if (measuring) { config.timings = &timings; timings.record("load_and_constraints", loadStart); }
    runRules(design.get(), report, config);
    if (measuring) {
        // Measurements are written even for a failed run: that is when they matter most.
        std::ofstream out(opts.timingsFile);
        out << timings.toJson() << '\n';
        if (!out) report.setExecutionError("could not write timings to '" + opts.timingsFile + "'");
    }
    if (design && design->hasCaseMode()) {
        json::Value modes = json::Value::array();
        for (size_t i = 0; i < design->caseResults.size(); ++i)
            for (const auto& name : design->caseResults[i].names) {
                json::Value mode = json::Value::object();
                mode.set("object", name).set("value", config.constraints.caseAnalyses[i].value);
                modes.push(mode);
            }
        report.addInput("case_analysis", modes);
        auto& selected = model::getModel(*design);
        report.addInput("case_pruned_branches", static_cast<int>(selected.casePrunedBranches));
        report.addInput("case_pruned_expressions", static_cast<int>(selected.casePrunedExpressions));
    }
    // Nothing below reads the model, and on a large design it is most of the memory in use.
    if (design) design->model.reset();
    report.finalize();

    WaiverSet waivers;
    if (design && opts.sourceWaivers) waivers = sourceWaivers(*design, opts.pathRoot);
    bool haveWaivers = false;
    for (auto& wf : opts.waiverFiles) {
        std::string text, err;
        WaiverSet one;
        if (!readFile(wf, text) || !WaiverSet::parse(text, one, err, std::filesystem::path(wf).filename().string())) {
            Diagnostic d;
            d.ruleId = "CST-006";
            d.severity = Severity::Error;
            d.identityKey = "unreadable-waiver";
            d.identityArguments = {std::filesystem::absolute(wf).string()};
            d.message = "waiver file '" + std::filesystem::path(wf).filename().string() + "' could not be loaded" + (err.empty() ? "" : ": " + err);
            d.nextStep = "Fix the waiver file; no waiver from it was applied.";
            report.add(std::move(d));
            report.setExecutionError("waiver file could not be loaded");
            continue;
        }
        for (auto& w : one.waivers()) waivers.mutableWaivers().push_back(w);
        haveWaivers = true;
    }
    (void)haveWaivers;
    applyWaivers(report, waivers, opts.today.empty() ? currentDateIso() : opts.today);
    for (auto& rc : report.mutableRuleCoverage())
        if (rc.ruleId == "CST-006" || rc.ruleId == "CST-022") rc.objectsEvaluated = waivers.waivers().size();
    report.finalize();
    // Fingerprints and waivers use the original diagnostic identity. Overrides
    // are applied only after waiver matching and never alter coverage.
    for (auto& d : report.mutableDiagnostics()) {
        auto it = config.severityOverrides.find(d.ruleId);
        // A primary finding stands for every rule it absorbed: the highest of their severities decides, and a rule without an
        // override keeps the severity it has. Lowering only one description therefore does not lower the crossing.
        bool absorbedOverride = false;
        for (const auto& rule : d.alsoRules) absorbedOverride = absorbedOverride || config.severityOverrides.count(rule) != 0;
        if (it == config.severityOverrides.end() && !absorbedOverride) continue;
        d.originalSeverity = d.severity;
        Severity effective = it != config.severityOverrides.end() ? it->second : d.severity;
        for (const auto& rule : d.alsoRules) {
            auto other = config.severityOverrides.find(rule);
            const Severity fact = other != config.severityOverrides.end() ? other->second : d.severity;
            if (fact > effective) {
                effective = fact;
                d.severityOverrideReason = "rule " + rule + " also describes this crossing and keeps this severity";
            }
        }
        d.requestedSeverity = effective;
        if (effective < d.severity && (d.severity == Severity::Fatal ||
            (d.origin == "slang" && d.severity >= Severity::Error) ||
            (d.severity >= Severity::Error && (d.ruleId == "LNT-001" || d.ruleId == "CST-006" || d.ruleId == "CST-018")))) {
            effective = d.severity;
            d.severityOverrideReason = "fatal, frontend or invalid input errors cannot be downgraded";
        } else if (effective < d.severity && effective < Severity::Warning &&
                   (d.confidence == Confidence::Unresolved || d.proofState == ProofState::Unknown || d.proofState == ProofState::NotAttempted)) {
            effective = Severity::Warning;
            d.severityOverrideReason = "unresolved findings, unknown proofs and obligations have a WARNING floor";
        }
        d.severity = effective;
        if (d.disposition != Disposition::Waived && d.disposition != Disposition::Resolved) {
            if (effective == Severity::Warning || (d.originalSeverity >= Severity::Error && effective < Severity::Error))
                d.disposition = Disposition::ReviewRequired;
            else if (effective >= Severity::Error) d.disposition = Disposition::Open;
        }
    }
    return report;
}

} // namespace sverity
