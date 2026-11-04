// CDC analyses of declared library cells and black boxes (--library kinds clock_gate, ram, blackbox) and of the ports of unknown
// black boxes. Technology neutral: the library names ports and roles only. An undeclared cell is never assumed safe: it stays an
// unresolved black box (CDC-023, CST-005).
//   CDC-035  library RAM written and read in different clock domains
//   CDC-046  black-box input fed from another clock domain
//   CDC-020  enable of a library clock gate launched in another domain
//   CDC-045  library clock gate whose enable is not latched inside the cell
#include "rules/cdc/cdc_shared.h"

namespace sverity::cdc {

int Shared::domainOfClockNode(NodeId clk) const {
    Root r = m->traceRoot(clk);
    for (size_t i = 0; i < m->domains.size(); i++)
        if (m->domains[i].root == r.node) return static_cast<int>(i);
    return -1;
}

void Shared::libraryCells() {
    auto sevOf = [](Relation rel) {
        return rel == Relation::Async ? Severity::Error : rel == Relation::Exclusive ? Severity::Info : Severity::Warning;
    };
    auto portDomain = [&](const std::vector<BlackBoxPort>& ports, const std::string& clockName) -> int {
        for (auto& p : ports)
            if (p.name == clockName && !p.nodes.empty()) return domainOfClockNode(p.nodes[0]);
        return -1;
    };
    // one crossing finding: data launched in `src`, captured or consumed in `dst`
    auto crossing = [&](const char* rule, int64_t src, int64_t dst, const std::string& what, const SourceLocation& loc, const std::string& hier) {
        if (src == dst) return;
        Relation rel = relation(static_cast<uint32_t>(src), static_cast<uint32_t>(dst));
        if (rel == Relation::Same) return;
        Diagnostic d = mk(rule, sevOf(rel), what + " [" + relName(rel) + "]: the crossing is unsynchronised", loc, hier,
                          rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
        d.evidence.push_back({"source_domain", m->domains[src].name, {}});
        d.evidence.push_back({"destination_domain", m->domains[dst].name, {}});
        d.nextStep = rel == Relation::Unknown
            ? "Declare the clock intent with --sdc and review whether the crossing needs synchronisation."
            : "Review receiving-domain synchronisation or justify a waiver; declaring clocks does not repair the crossing.";
        add(std::move(d));
    };
    auto unresolvedCrossing = [&](const char* rule, const std::string& what, const SourceLocation& loc, const std::string& hier, const char* scope) {
        Diagnostic d = mk(rule, Severity::Warning, what + ": whether it crosses a clock domain is unresolved, not safe", loc, hier, Confidence::Unresolved);
        d.nextStep = "Declare the port's clock in the --library file (\"clock\": <clock port>) and constrain the primary inputs.";
        add(std::move(d));
        report->markIncomplete(scope, what);
    };

    // ---- CDC-035: library RAM
    for (auto& ram : m->rams) {
        evaluated["CDC-035"]++;
        if (ram.writeDomain < 0 || ram.readDomain < 0) {
            Diagnostic d = mk("CDC-035", Severity::Warning,
                              "library RAM '" + ram.module + "' at " + ram.hierarchy + ": the write or read clock does not resolve to a clock root, so whether it crosses clock domains is unresolved",
                              ram.loc, ram.hierarchy, Confidence::Unresolved);
            d.nextStep = "Connect the RAM clock ports to resolvable clocks or declare the clocks with --sdc.";
            add(std::move(d));
            report->markIncomplete("clock-extraction", "library RAM " + ram.hierarchy + " clock unresolved");
        }
        else if (ram.writeDomain != ram.readDomain) {
            Relation rel = relation(static_cast<uint32_t>(ram.writeDomain), static_cast<uint32_t>(ram.readDomain));
            if (rel != Relation::Same) {
                Diagnostic d = mk("CDC-035", rel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                                  "library RAM '" + ram.module + "' at " + ram.hierarchy + " is written in clock domain " + m->domains[ram.writeDomain].name + " and read in " +
                                      m->domains[ram.readDomain].name + " [" + relName(rel) + "]: correctness depends on the pointer/handshake protocol around it",
                                  ram.loc, ram.hierarchy, Confidence::Unresolved);
                d.proofState = ProofState::NotAttempted;
                if (rel == Relation::Exclusive) d.evidence.push_back({"trust_model", "the two clocks are declared exclusive by constraint (trusted, not verified)", {}});
                d.evidence.push_back({"write_clock", m->domains[ram.writeDomain].name, {}});
                d.evidence.push_back({"read_clock", m->domains[ram.readDomain].name, {}});
                d.evidence.push_back({"proof_obligation", "read-during-write behaviour and the access protocol are not verified", {}});
                d.nextStep = "Guard dual-clock memory access with a recognised async-FIFO pointer scheme; declare the clock relationship.";
                add(std::move(d));
            }
        }
        for (auto& p : ram.ports) {
            if (p.nodes.empty() || p.role == PortRole::Clock) continue;
            std::string who = "library RAM '" + ram.module + "' at " + ram.hierarchy + " port " + p.name;
            bool isInput = p.dir != PortDir::Out && (p.role == PortRole::DataIn || p.role == PortRole::WriteEnable || p.role == PortRole::Address);
            bool isOutput = p.dir != PortDir::In && p.role == PortRole::DataOut;
            int64_t ref = !p.clock.empty() ? portDomain(ram.ports, p.clock)
                          : (p.role == PortRole::DataIn || p.role == PortRole::WriteEnable) ? ram.writeDomain
                          : p.role == PortRole::DataOut ? ram.readDomain : -1;
            if (isInput) {
                Cone c = cones_->ofNodes(p.nodes);
                if (ref < 0) {
                    if (!c.flops.empty() || !c.inputs.empty())
                        unresolvedCrossing("CDC-035", who + " is fed by registers or inputs but its clock is not declared", ram.loc, ram.hierarchy, "clock-extraction");
                    continue;
                }
                for (auto& s : c.flops) {
                    const Flop& sf = m->flops[m->flopOfQ.at(s.q)];
                    if (!sf.clockResolved) { unresolvedCrossing("CDC-035", who + " is fed by a register with an unidentified clock", ram.loc, ram.hierarchy, "clock-extraction"); continue; }
                    crossing("CDC-035", sf.domain, ref, who + " is fed by " + m->nameOf(s.q) + " launched in clock domain " + m->domains[sf.domain].name +
                                                            " but is synchronous to " + m->domains[ref].name, ram.loc, ram.hierarchy);
                }
                if (!c.inputs.empty())
                    unresolvedCrossing("CDC-035", who + " is fed by primary input " + m->nameOf(c.inputs[0].q) + " with no declared relation to " + m->domains[ref].name, ram.loc, ram.hierarchy, "constraints");
            }
            if (isOutput) {
                std::vector<NodeId> st(p.nodes.begin(), p.nodes.end());
                std::set<NodeId> seen(st.begin(), st.end()), dests;
                while (!st.empty()) {
                    NodeId n = st.back();
                    st.pop_back();
                    for (auto ei : m->out[n]) {
                        const Edge& e = m->edges[ei];
                        if (e.type == EdgeType::Seq && m->flopOfQ.count(e.to)) { dests.insert(e.to); continue; }
                        if (e.type == EdgeType::Comb && seen.insert(e.to).second) st.push_back(e.to);
                    }
                }
                for (auto q : dests) {
                    const Flop& df = *m->flopAt(q);
                    if (ref < 0 || !df.clockResolved) {
                        unresolvedCrossing("CDC-035", who + " read data is captured by " + m->nameOf(q) + " whose clock or the RAM read clock is unresolved", ram.loc, ram.hierarchy, "clock-extraction");
                        continue;
                    }
                    crossing("CDC-035", ref, df.domain, who + " read data (domain " + m->domains[ref].name + ") is captured by " + m->nameOf(q) + " in clock domain " + m->domains[df.domain].name,
                             ram.loc, ram.hierarchy);
                }
            }
        }
    }

    // ---- CDC-046: black-box inputs
    for (auto& bb : m->blackBoxes) {
        evaluated["CDC-046"]++;
        if (bb.declared) {
            std::vector<const BlackBoxPort*> clocks;
            for (auto& p : bb.ports) if (p.role == PortRole::Clock && !p.nodes.empty()) clocks.push_back(&p);
            for (auto& p : bb.ports) {
                if (p.dir == PortDir::Out || p.role == PortRole::Clock || p.role == PortRole::Reset || p.nodes.empty()) continue;
                std::string who = "black box '" + bb.module + "' at " + bb.hierarchy + " port " + p.name;
                Cone c = cones_->ofNodes(p.nodes);
                if (c.flops.empty() && c.inputs.empty()) continue;
                int ref = -1;
                if (!p.clock.empty()) ref = portDomain(bb.ports, p.clock);
                else if (clocks.size() == 1) ref = domainOfClockNode(clocks[0]->nodes[0]);
                if (ref < 0) {
                    unresolvedCrossing("CDC-046", who + " is fed by registers or inputs but the clock it is synchronous to is not declared or resolved", bb.loc, bb.hierarchy, "blackboxes");
                    continue;
                }
                for (auto& s : c.flops) {
                    const Flop& sf = m->flops[m->flopOfQ.at(s.q)];
                    if (!sf.clockResolved) { unresolvedCrossing("CDC-046", who + " is fed by a register with an unidentified clock", bb.loc, bb.hierarchy, "clock-extraction"); continue; }
                    crossing("CDC-046", sf.domain, ref, who + " receives " + m->nameOf(s.q) + " launched in clock domain " + m->domains[sf.domain].name +
                                                            " but is synchronous to " + m->domains[ref].name, bb.loc, bb.hierarchy);
                }
                if (!c.inputs.empty())
                    unresolvedCrossing("CDC-046", who + " receives primary input " + m->nameOf(c.inputs[0].q) + " with no declared relation to " + m->domains[ref].name, bb.loc, bb.hierarchy, "constraints");
            }
            continue;
        }
        std::vector<NodeId> inputs;
        for (auto& p : bb.ports) for (auto n : p.nodes) inputs.push_back(n);
        if (inputs.empty()) continue;
        Cone c = cones_->ofNodes(inputs);
        std::set<uint32_t> srcDomains;
        bool unidentified = false;
        for (auto& s : c.flops) {
            const Flop& sf = m->flops[m->flopOfQ.at(s.q)];
            if (sf.clockResolved) srcDomains.insert(sf.domain);
            else unidentified = true;
        }
        if (srcDomains.size() >= 2) {
            std::string names;
            for (auto sd : srcDomains) names += (names.empty() ? "" : ", ") + m->domains[sd].name;
            Diagnostic d = mk("CDC-046", Severity::Warning,
                              "black box '" + bb.module + "' at " + bb.hierarchy + " receives signals launched in clock domain(s) " + names +
                                  "; its port directions and clocks are unknown: whether it crosses clock domains is unresolved, not safe",
                              bb.loc, bb.hierarchy, Confidence::Unresolved);
            d.nextStep = "Declare the cell in a --library file (kind blackbox, port directions, roles and clocks) or provide its source.";
            add(std::move(d));
            report->markIncomplete("blackboxes", "black box " + bb.hierarchy + ": input domains unresolved");
        }
        if (unidentified) {
            Diagnostic d = mk("CDC-046", Severity::Warning,
                              "black box '" + bb.module + "' at " + bb.hierarchy + " receives a signal from a register with an unidentified clock: crossing status is unresolved",
                              bb.loc, bb.hierarchy, Confidence::Unresolved);
            add(std::move(d));
            report->markIncomplete("clock-extraction", "black box " + bb.hierarchy + " input from unidentified clock");
        }
    }

    // ---- library clock gates
    for (auto& g : m->clockGates) {
        int gd = g.clk.empty() ? -1 : domainOfClockNode(g.clk[0]);
        evaluated["CDC-020"]++;
        evaluated["CDC-045"]++;
        if (!g.latched) {
            Diagnostic d = mk("CDC-045", Severity::Warning,
                              "library clock gate '" + g.module + "' at " + g.hierarchy + " does not latch its enable inside the cell (library declaration): a late or changing enable can truncate or glitch the clock pulse",
                              g.loc, g.hierarchy, Confidence::Established);
            d.nextStep = "Use a latch-based integrated clock gate or declare enable_latched in the library when the cell really contains the latch.";
            add(std::move(d));
        }
        else {
            Diagnostic d = mk("CDC-045", Severity::Info, "library clock gate '" + g.module + "' at " + g.hierarchy + " latches its enable inside the cell (library declaration, trusted)",
                              g.loc, g.hierarchy, Confidence::Heuristic);
            d.proofState = ProofState::NotAttempted;
            d.evidence.push_back({"trust_model", "the library declaration is trusted: the cell's latch phase and timing are not verified", {}});
            add(std::move(d));
        }
        if (gd < 0) {
            Diagnostic d = mk("CDC-020", Severity::Warning, "library clock gate '" + g.module + "' at " + g.hierarchy + ": its clock does not resolve to a clock root, so the enable crossing is unresolved",
                              g.loc, g.hierarchy, Confidence::Unresolved);
            add(std::move(d));
            report->markIncomplete("clock-extraction", "library clock gate " + g.hierarchy + " clock unresolved");
            continue;
        }
        Cone c = cones_->ofNodes(g.enable);
        for (auto& s : c.flops) {
            const Flop& sf = m->flops[m->flopOfQ.at(s.q)];
            if (!sf.clockResolved) {
                add(mk("CDC-020", Severity::Warning, "enable of library clock gate '" + g.module + "' at " + g.hierarchy + " comes from a register with an unidentified clock",
                       g.loc, g.hierarchy, Confidence::Unresolved));
                continue;
            }
            if (sf.domain == static_cast<uint32_t>(gd)) continue;
            Relation rel = relation(sf.domain, static_cast<uint32_t>(gd));
            if (rel == Relation::Same) continue;
            Diagnostic d = mk("CDC-020", sevOf(rel),
                              "enable " + m->nameOf(s.q) + " of library clock gate '" + g.module + "' at " + g.hierarchy + " is launched in clock domain " + m->domains[sf.domain].name +
                                  " but the gate is clocked by " + m->domains[gd].name + " [" + relName(rel) + "]: the gate can glitch the clock",
                              procOf(sf).loc, procOf(sf).instancePath, rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
            d.nextStep = "Synchronise the enable to the gate's clock domain.";
            add(std::move(d));
        }
        for (auto& in : c.inputs)
            add(mk("CDC-020", Severity::Warning, "enable " + m->nameOf(in.q) + " of library clock gate '" + g.module + "' at " + g.hierarchy + " is a primary input with no declared relation to the clock",
                   g.loc, g.hierarchy, Confidence::Unresolved));
    }
}

} // namespace sverity::cdc
