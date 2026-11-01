// CDC protocol / structure recognition helpers used by the crossing analysis (see cdc_shared.h):
// Gray-coded pointers, binary counters, payloads qualified by a synchronised control, SDC exception lookup.
#include "rules/cdc/cdc_shared.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/text/SourceManager.h"

namespace sverity::cdc {

using namespace slang;
using namespace slang::ast;
using namespace sverity::model;

namespace {

const Expression& strip(const Expression& e) {
    const Expression* x = &e;
    while (x->kind == ExpressionKind::Conversion) x = &x->as<ConversionExpression>().operand();
    return *x;
}

bool sameExpr(const Expression& a, const Expression& b) {
    auto* sa = a.getSymbolReference();
    auto* sb = b.getSymbolReference();
    if (sa && sb && sa == sb && a.kind == b.kind) return true;
    return a.syntax && b.syntax && a.syntax->toString() == b.syntax->toString();
}

/// `x >> 1` (logical or arithmetic shift by the constant 1): returns x.
const Expression* shiftedByOne(const Expression& e, EvalContext& ec) {
    const Expression& x = strip(e);
    if (x.kind != ExpressionKind::BinaryOp) return nullptr;
    auto& b = x.as<BinaryExpression>();
    if (b.op != BinaryOperator::LogicalShiftRight && b.op != BinaryOperator::ArithmeticShiftRight) return nullptr;
    auto cv = b.right().eval(ec);
    if (!cv.isInteger() || cv.integer().hasUnknown() || cv.integer() != SVInt(32, 1, false)) return nullptr;
    return &b.left();
}

/// A single-bit select of a symbol with a constant index: returns (symbol, index).
bool constSelect(const Expression& e, EvalContext& ec, const ValueSymbol*& sym, int64_t& idx) {
    const Expression& x = strip(e);
    if (x.kind != ExpressionKind::ElementSelect) return false;
    auto& sel = x.as<ElementSelectExpression>();
    auto* ref = sel.value().getSymbolReference();
    sym = ref ? ref->as_if<ValueSymbol>() : nullptr;
    auto cv = sel.selector().eval(ec);
    if (!sym || !cv.isInteger() || cv.integer().hasUnknown()) return false;
    auto v = cv.integer().as<int64_t>();
    if (!v) return false;
    idx = *v;
    return true;
}

/// binary-to-Gray: `b ^ (b >> 1)` in either operand order, or an XOR of two adjacent bit-selects of one vector.
bool isBinaryToGray(const Expression& rhs, EvalContext& ec) {
    const Expression& e = strip(rhs);
    if (e.kind != ExpressionKind::BinaryOp) return false;
    auto& b = e.as<BinaryExpression>();
    if (b.op != BinaryOperator::BinaryXor) return false;
    if (auto* x = shiftedByOne(b.right(), ec); x && sameExpr(*x, b.left())) return true;
    if (auto* x = shiftedByOne(b.left(), ec); x && sameExpr(*x, b.right())) return true;
    const ValueSymbol *s1 = nullptr, *s2 = nullptr;
    int64_t i1 = 0, i2 = 0;
    return constSelect(b.left(), ec, s1, i1) && constSelect(b.right(), ec, s2, i2) && s1 == s2 && (i1 - i2 == 1 || i2 - i1 == 1);
}

// A symbol is Gray-coded only if EVERY assignment to it is a whole-symbol binary-to-Gray conversion: one partial or
// differently-shaped assignment (raw data on another branch, a single converted bit) disqualifies it.
struct GrayFinder : ASTVisitor<GrayFinder, VisitFlags::AllGood> {
    std::unordered_set<const ValueSymbol*>& out;
    std::unordered_set<const ValueSymbol*> grayWhole, other;
    Compilation& comp;
    GrayFinder(std::unordered_set<const ValueSymbol*>& o, Compilation& c) : out(o), comp(c) {}
    void handle(const AssignmentExpression& a) {
        ASTContext ac(comp.getRoot(), LookupLocation::max);
        EvalContext ec(ac);
        if (auto* ref = a.left().getSymbolReference(); ref && ref->as_if<ValueSymbol>()) {
            auto* sym = ref->as_if<ValueSymbol>();
            bool whole = a.left().kind == ExpressionKind::NamedValue;
            if (whole && isBinaryToGray(a.right(), ec)) grayWhole.insert(sym);
            else if (a.right().eval(ec).bad()) other.insert(sym);   // constant assignments (reset values) are neutral
        }
        visitDefault(a);
    }
    void finish() { for (auto* s : grayWhole) if (!other.count(s)) out.insert(s); }
};

} // namespace

void Shared::findGrayNets() {
    if (!design_ || !design_->compilation) return;
    GrayFinder f(grayNets_, *design_->compilation);
    design_->compilation->getRoot().visit(f);
    f.finish();
}

bool Shared::isGrayFlop(NodeId q) const {
    NodeId cur = q;
    for (int guard = 0; guard < 16; guard++) {
        if (m->nodes[cur].sym && grayNets_.count(m->nodes[cur].sym)) return true;
        // follow a plain register-transfer / alias to its single source
        NodeId next = kNoNode;
        for (auto ei : m->in[cur]) {
            const Edge& e = m->edges[ei];
            if (e.type == EdgeType::AsyncCtl || e.ctrl) continue;
            if (!e.exact || next != kNoNode) return false;
            next = e.from;
        }
        if (next == kNoNode) return false;
        cur = next;
    }
    return false;
}

bool Shared::hasCtrlEdge(const Flop& d) const {
    for (auto ei : m->in[d.q]) {
        const Edge& e = m->edges[ei];
        if (e.type == EdgeType::Seq && e.ctrl) return true;
    }
    return false;
}

bool Shared::ctrlFromSynced(const Flop& d, uint32_t srcDomain) const {
    auto git = m->guards.find(d.q);
    if (git == m->guards.end() || git->second.empty()) return false;
    auto reaches = [&](NodeId start) {
        std::vector<NodeId> st{start};
        std::unordered_set<NodeId> seen{start};
        for (size_t i = 0; i < st.size() && st.size() < 64; i++) {
            NodeId n = st[i];
            auto it = syncedOut_.find(n);
            if (it != syncedOut_.end() && it->second.srcDomain == srcDomain) return true;
            if (m->flopOfQ.count(n)) continue;   // a different register: stop (only combinational qualifier logic is followed)
            for (auto pi : m->in[n]) {
                const Edge& pe = m->edges[pi];
                if (pe.type == EdgeType::Comb && seen.insert(pe.from).second) st.push_back(pe.from);
            }
        }
        return false;
    };
    // Every assignment that moves data into the register must be guarded, and every guard must be a synchronised control:
    // an unguarded assignment, or an enable OR-ed with an unsynchronised signal, is not a qualifier.
    for (auto& g : git->second) {
        if (g.empty()) return false;
        for (auto n : g) if (!reaches(n)) return false;
    }
    return true;
}

bool Shared::counterLike(const std::set<NodeId>& srcBits) {
    for (auto q : srcBits) {
        auto it = m->flopOfQ.find(q);
        if (it == m->flopOfQ.end()) continue;
        Cone c = coneOf(m->flops[it->second]);
        for (auto& s : c.flops)
            if (s.q != q && srcBits.count(s.q)) return true;   // the next value of one bit depends on another bit of the bus: carry chain
    }
    return false;
}

std::vector<const sdc::PathException*> Shared::exceptionsFor(NodeId src, NodeId dst, bool anyKind,
                                                              sdc::PathException::Kind kind) const {
    std::vector<const sdc::PathException*> out;
    auto matches = [&](const std::vector<sdc::ObjectRef>& refs, NodeId n) {
        std::string full = stripBit(m->nameOf(n));
        for (auto& r : refs) {
            if (r.kind == "get_clocks" || r.kind == "all_clocks") {      // clock objects: match the SDC clock of the register's domain
                const Flop* f = m->flopAt(n);
                if (r.kind == "all_clocks" || (f && f->clockResolved && globMatch(r.pattern, domainClockName(f->domain)))) return true;
                continue;
            }
            if (rel_->nameMatches(r.kind, r.pattern, n, full)) return true;
        }
        return false;
    };
    for (auto& ex : cfg->constraints.exceptions) {
        if (!anyKind && ex.kind != kind) continue;
        if (ex.from.empty() && ex.to.empty()) {
            if (!ex.through.empty() && (matches(ex.through, src) || matches(ex.through, dst))) out.push_back(&ex);   // -through: matched by name only
            continue;
        }
        // -through is not path-evaluated: an exception with -through is reported as possibly covering the crossing
        if ((ex.from.empty() || matches(ex.from, src)) && (ex.to.empty() || matches(ex.to, dst))) out.push_back(&ex);
    }
    return out;
}

} // namespace sverity::cdc

// ======================================================================================================
// Structure analyses over the crossing groups (reconvergence, clocks built from logic, async controls,
// toggle / handshake / FIFO protocols, memories, attributes). Every classification is structural: a
// recognised protocol is reported INFO together with its proof obligation, never as proven safe.
// ======================================================================================================
#include "slang/ast/symbols/InstanceSymbols.h"

namespace sverity::cdc {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

double Shared::periodNs(uint32_t domain) const {
    const std::string& n = domainClockName(domain);
    if (n.empty()) return 0;
    for (auto& c : cfg->constraints.clocks)
        if ((c.name.empty() ? (c.sources.empty() ? std::string() : c.sources[0].pattern) : c.name) == n) return c.periodNs;
    return 0;
}

void Shared::analyseStructure() {
    for (const char* r : {"CDC-010", "CDC-011", "CDC-036", "CDC-019", "CDC-020", "CDC-030", "CDC-031", "CDC-021", "CDC-043",
                          "CDC-012", "CDC-013", "CDC-014", "CDC-015", "CDC-024", "CDC-034", "CDC-035", "CDC-037", "CDC-038",
                          "CDC-040", "CDC-041", "CDC-042", "CDC-044", "CDC-023", "RDC-023", "RDC-024"})
        evaluated[r] += 0;
    sequentialElements();
    reconvergence();
    clockStructure();
    asyncControls();
    edgeStructure();
    protocols();
}

// --- CDC-001: level-sensitive elements have no clock; never silently skipped ------------------------------------
void Shared::sequentialElements() {
    for (size_t pi = 0; pi < m->procs.size(); pi++) {
        if (m->procs[pi].kind != ProcKind::Latch) continue;
        evaluated["CDC-001"]++;
        Diagnostic d = mk("CDC-001", Severity::Warning, "level-sensitive element (latch) has no clock edge: it is not assigned to a clock domain",
                          m->procs[pi].loc, m->procs[pi].instancePath, Confidence::Unresolved);
        d.nextStep = "Review how data crosses this latch; CDC analysis treats its output as an unclassified source.";
        add(std::move(d));
        report->markIncomplete("clock-extraction", "latch at " + m->procs[pi].loc.file + ":" + std::to_string(m->procs[pi].loc.line));
    }
}

// --- CDC-010 / 011 / 036: several asynchronous influences meeting in one destination cone -----------------------
void Shared::reconvergence() {
    std::set<std::string> done;
    // synchronised signals that reach a destination-domain register directly: followed one register stage further, so that
    // reconvergence behind a pipeline register is also found
    std::map<NodeId, std::map<std::string, const SyncOut*>> derived;
    for (uint32_t di = 0; di < m->flops.size(); di++) {
        const Flop& d = m->flops[di];
        if (!d.clockResolved) continue;
        Cone c = coneOf(d);
        for (auto& s : c.flops) {
            const Flop& sf = m->flops[m->flopOfQ.at(s.q)];
            if (!sf.clockResolved || sf.domain != d.domain) continue;
            auto it = syncedOut_.find(s.q);
            if (it != syncedOut_.end()) derived[d.q][m->nameOf(it->second.srcQ)] = &it->second;
        }
    }
    for (uint32_t di = 0; di < m->flops.size(); di++) {
        const Flop& d = m->flops[di];
        if (!d.clockResolved) continue;
        Cone c = coneOf(d);
        std::map<std::string, const SyncOut*> synced;
        std::set<std::string> raw;
        std::set<uint32_t> rawDomains;
        for (auto& s : c.flops) {
            const Flop& sf = m->flops[m->flopOfQ.at(s.q)];
            if (!sf.clockResolved) continue;
            if (sf.domain == d.domain) {
                auto it = syncedOut_.find(s.q);
                if (it != syncedOut_.end()) synced[m->nameOf(it->second.srcQ)] = &it->second;   // per source bit: bits of one bus are separate signals
                else if (auto dv = derived.find(s.q); dv != derived.end())                       // a pipeline register behind synchronisers
                    for (auto& [k, so] : dv->second) synced[k] = so;
            } else if (relation(sf.domain, d.domain) != Relation::Same && !ctrlFromSynced(d, sf.domain)) {
                raw.insert(stripBit(m->nameOf(s.q)));
                rawDomains.insert(sf.domain);
            }
        }
        if (synced.size() + raw.size() < 2) continue;
        const char* rule = !raw.empty() ? "CDC-036" : nullptr;
        if (!rule) {
            std::set<uint32_t> doms;
            for (auto& [n, so] : synced) doms.insert(so->srcDomain);
            rule = doms.size() > 1 ? "CDC-011" : "CDC-010";
        }
        std::string dst = stripBit(m->nameOf(d.q));
        if (!done.insert(std::string(rule) + dst).second) continue;
        evaluated[rule]++;
        std::string list;
        for (auto& [n, so] : synced) list += (list.empty() ? "" : ", ") + n + " (synchronised from " + m->domains[so->srcDomain].name + ")";
        for (auto& n : raw) list += (list.empty() ? "" : ", ") + n + " (NOT synchronised)";
        Diagnostic x = mk(rule, Severity::Warning,
                          "register " + dst + " combines " + std::to_string(synced.size() + raw.size()) + " asynchronous influences before sampling: " + list +
                          (std::string(rule) == "CDC-010" ? "; independently synchronised signals from one domain can arrive in different cycles and reconverge inconsistently"
                           : std::string(rule) == "CDC-011" ? "; signals from different source domains reconverge in one cone"
                                                            : "; unsynchronised signals are combined by logic, so glitches/skew are sampled"),
                          procOf(d).loc, procOf(d).instancePath, Confidence::Heuristic);
        x.signals = {dst};
        x.proofState = ProofState::NotAttempted;
        x.nextStep = "Merge the related signals in the source domain and synchronise once (or use a handshake/FIFO) before combining them.";
        add(std::move(x));
    }
}

// --- clocks built from logic: muxes, gates, divided and generated clocks --------------------------------------------
namespace {
// Latch processes whose enable condition is a negation (`if (!clk)`, `if (~clk)`, `if (clk == 0)`): transparent while low.
struct LowPhaseLatches : ASTVisitor<LowPhaseLatches, VisitFlags::AllGood> {
    const slang::SourceManager& sm;
    std::set<std::string> keys;
    explicit LowPhaseLatches(const slang::SourceManager& s) : sm(s) {}
    void handle(const ProceduralBlockSymbol& p) {
        if (p.procedureKind == ProceduralBlockKind::AlwaysLatch || p.procedureKind == ProceduralBlockKind::Always) {
            const Statement* b = &p.getBody();
            while (b && (b->kind == StatementKind::Timed || b->kind == StatementKind::Block || b->kind == StatementKind::List)) {
                if (b->kind == StatementKind::Timed) b = &b->as<TimedStatement>().stmt;
                else if (b->kind == StatementKind::Block) b = &b->as<BlockStatement>().body;
                else { auto& l = b->as<StatementList>().list; b = l.empty() ? nullptr : l[0]; }
            }
            if (b && b->kind == StatementKind::Conditional && !b->as<ConditionalStatement>().conditions.empty()) {
                const Expression* e = b->as<ConditionalStatement>().conditions[0].expr;
                while (e->kind == ExpressionKind::Conversion) e = &e->as<ConversionExpression>().operand();
                bool low = false;
                if (e->kind == ExpressionKind::UnaryOp) { auto op = e->as<UnaryExpression>().op; low = op == UnaryOperator::LogicalNot || op == UnaryOperator::BitwiseNot; }
                if (low) {
                    auto l = p.location;
                    keys.insert(sm.getFullPath(l.buffer()).generic_string() + ":" + std::to_string(sm.getLineNumber(l)) + ":" + std::to_string(sm.getColumnNumber(l)));
                }
            }
        }
        visitDefault(p);
    }
};
} // namespace

void Shared::clockStructure() {
    LowPhaseLatches lowLatches(*design_->compilation->getSourceManager());
    design_->compilation->getRoot().visit(lowLatches);
    std::set<NodeId> clockRoots;
    for (auto& dm : m->domains) clockRoots.insert(dm.root);
    // primary inputs declared as clock sources in the SDC, or named like clocks (heuristic), are clock leaves too
    for (NodeId n = 0; n < m->nodes.size(); n++) {
        if (n >= m->topInput.size() || !m->topInput[n] || !m->nodes[n].sym) continue;
        std::string nm = lower(std::string(m->nodes[n].sym->name));
        if (!rel_->clockNameOfNode(n).empty() || nm.find("clk") != std::string::npos || nm.find("clock") != std::string::npos) clockRoots.insert(n);
    }
    auto domainOfRoot = [&](NodeId n) -> int {
        for (size_t j = 0; j < m->domains.size(); j++) if (m->domains[j].root == n) return static_cast<int>(j);
        return -1;
    };
    for (size_t i = 0; i < m->domains.size(); i++) {
        const Domain& dom = m->domains[i];
        // SDC generated clock on this root: the master/generated relationship is declared but not derived
        if (!domainClockName(static_cast<uint32_t>(i)).empty())
            for (auto& ck : cfg->constraints.clocks)
                if (ck.generated && (ck.name.empty() ? (ck.sources.empty() ? std::string() : ck.sources[0].pattern) : ck.name) == domainClockName(static_cast<uint32_t>(i))) {
                    evaluated["CDC-030"]++;
                    Diagnostic d = mk("CDC-030", Severity::Warning,
                                      "clock '" + dom.name + "' is declared with create_generated_clock (master '" + ck.masterClock + "', divide_by '" + ck.divideBy +
                                      "', multiply_by '" + ck.multiplyBy + "'): edge alignment between master and generated clock is a timing-tool assumption; "
                                      "crossings between them stay UNKNOWN until a clock relation is declared",
                                      ck.loc, {}, Confidence::Unresolved);
                    d.nextStep = "State the relationship explicitly (set_clock_groups) or synchronise crossings between master and generated clock.";
                    add(std::move(d));
                    report->markIncomplete("clock-relationships", "generated clock '" + dom.name + "' phase relationship is assumed, not derived");
                }
        if (dom.kind != RootKind::Logic) continue;
        // walk the combinational cone of the clock net
        std::vector<NodeId> clockLeaves, flopLeaves, inputLeaves;
        bool latch = false, bbox = false, directEnable = false;
        // states: (node, reached through a latch output). An enable that reaches the gate without passing a latch is direct.
        std::set<std::pair<NodeId, bool>> seen{{dom.root, false}};
        std::vector<std::pair<NodeId, bool>> st{{dom.root, false}};
        while (!st.empty()) {
            auto [n, viaLatch] = st.back(); st.pop_back();
            if (n != dom.root && clockRoots.count(n)) { clockLeaves.push_back(n); continue; }
            if (m->flopOfQ.count(n)) { flopLeaves.push_back(n); directEnable = directEnable || !viaLatch; continue; }
            if (n < m->topInput.size() && m->topInput[n]) { inputLeaves.push_back(n); directEnable = directEnable || !viaLatch; continue; }
            if (n < m->blackBoxDriven.size() && m->blackBoxDriven[n]) bbox = true;
            for (auto ei : m->in[n]) {
                const Edge& e = m->edges[ei];
                if (e.type != EdgeType::Comb) continue;
                bool isLatch = m->procs[e.proc].kind == ProcKind::Latch;
                // a clock-gate latch is a latch process whose control (enable condition) is the clock itself
                if (isLatch && e.ctrl && clockRoots.count(e.from)) {
                    const auto& pl = m->procs[e.proc].loc;
                    if (lowLatches.keys.count(pl.file + ":" + std::to_string(pl.line) + ":" + std::to_string(pl.column))) latch = true;
                }
                if (isLatch && e.ctrl) continue;   // the latch's own control edge is not an enable path
                if (seen.insert({e.from, viaLatch || isLatch}).second) st.push_back({e.from, viaLatch || isLatch});
            }
        }
        if (directEnable) latch = false;   // an enable that bypasses the latch is not gated by it
        std::sort(clockLeaves.begin(), clockLeaves.end());
        clockLeaves.erase(std::unique(clockLeaves.begin(), clockLeaves.end()), clockLeaves.end());
        const SourceLocation& loc = m->nodes[dom.root].sym ? SourceLocation{} : SourceLocation{};
        (void)loc;
        // relationship between a register's clock domain and a clock leaf (which may not be the root of any domain)
        auto relLeaf = [&](uint32_t flopDomain, NodeId leaf) {
            int j = domainOfRoot(leaf);
            if (j >= 0) return relation(flopDomain, static_cast<uint32_t>(j));
            return rel_->relationByNames(domainClockName(flopDomain), rel_->clockNameOfNode(leaf));
        };
        auto leafName = [&](NodeId leaf) { int j = domainOfRoot(leaf); return j >= 0 ? m->domains[j].name : m->nameOf(leaf); };
        auto selectInfo = [&](std::vector<NodeId>& cl, std::string& what) {
            for (auto n : flopLeaves) what += (what.empty() ? "" : ", ") + m->nameOf(n);
            for (auto n : inputLeaves) what += (what.empty() ? "" : ", ") + m->nameOf(n) + " (primary input)";
            (void)cl;
        };
        if (clockLeaves.size() >= 2) {
            evaluated["CDC-019"]++; evaluated["CDC-031"]++;
            std::string sel;
            selectInfo(clockLeaves, sel);
            Diagnostic d = mk("CDC-019", Severity::Warning,
                              "clock '" + dom.name + "' is built from " + std::to_string(clockLeaves.size()) + " clocks by combinational logic (clock mux/combine)" +
                              (sel.empty() ? "" : "; select/enable inputs: " + sel) + ": it is not a recognised glitch-free clock multiplexer",
                              {}, {}, Confidence::Unresolved);
            d.nextStep = "Use a library glitch-free clock mux / clock-gating cell, or constrain the select so it only changes while the clocks are stopped.";
            for (auto cl : clockLeaves) d.evidence.push_back({"clock_input", m->nameOf(cl), {}});
            add(std::move(d));
            report->markIncomplete("clock-extraction", "clock '" + dom.name + "' is a clock mux/combine of unknown glitch behaviour");
            // select domain versus the clocks it switches between, and the output clock versus its inputs
            std::string issue;
            for (auto n : flopLeaves) {
                const Flop& sf = m->flops[m->flopOfQ.at(n)];
                if (!sf.clockResolved) { issue += "select " + m->nameOf(n) + " has an unidentified clock; "; continue; }
                for (auto cl : clockLeaves)
                    if (relLeaf(sf.domain, cl) != Relation::Same)
                        issue += "select " + m->nameOf(n) + " (domain " + m->domains[sf.domain].name + ") is not synchronous to " + leafName(cl) + "; ";
            }
            for (auto n : inputLeaves) issue += "select " + m->nameOf(n) + " is a primary input with no declared relation to the clocks; ";
            for (auto cl : clockLeaves)
                if (relLeaf(static_cast<uint32_t>(i), cl) == Relation::Unknown) issue += "relation of output clock to " + leafName(cl) + " is unknown; ";
            if (!issue.empty()) {
                Diagnostic x = mk("CDC-031", Severity::Warning, "clock mux '" + dom.name + "': " + issue + "the selection crosses clock domains", {}, {}, Confidence::Unresolved);
                x.nextStep = "Synchronise the select into each input clock domain (handshaked switch) and declare the output clock relationships.";
                add(std::move(x));
            }
        } else if (clockLeaves.size() == 1 && (!flopLeaves.empty() || !inputLeaves.empty() || bbox)) {
            evaluated["CDC-020"]++;
            std::string sel;
            selectInfo(clockLeaves, sel);
            if (bbox) {
                Diagnostic d = mk("CDC-020", Severity::Warning, "clock '" + dom.name + "' is gated through an unmodeled black box", {}, {}, Confidence::Unresolved);
                add(std::move(d));
            }
            for (auto n : flopLeaves) {
                const Flop& sf = m->flops[m->flopOfQ.at(n)];
                Relation rel = sf.clockResolved ? relLeaf(sf.domain, clockLeaves[0]) : Relation::Unknown;
                if (rel != Relation::Same) {
                    Diagnostic d = mk("CDC-020", rel == Relation::Async ? Severity::Error : rel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                                      "enable " + m->nameOf(n) + " of the gated clock '" + dom.name + "' is launched in another clock domain [" + relName(rel) +
                                      "]: the gate can glitch the clock", procOf(sf).loc, procOf(sf).instancePath,
                                      rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
                    d.nextStep = "Synchronise the enable to the gated clock's source domain and hold it with a latch-based clock gate.";
                    add(std::move(d));
                }
            }
            for (auto n : inputLeaves) {
                add(mk("CDC-020", Severity::Warning, "gated clock '" + dom.name + "' enable " + m->nameOf(n) + " is a primary input with no declared relation to the clock",
                       {}, {}, Confidence::Unresolved));
            }
            if (!latch && !flopLeaves.empty()) {
                Diagnostic d = mk("CDC-020", Severity::Warning,
                                  "clock '" + dom.name + "' is gated by combinational logic without an enable latch: a late or changing enable (" + sel +
                                  ") can truncate or glitch the clock pulse", {}, {}, Confidence::Heuristic);
                d.nextStep = "Use a latch-based integrated clock gate (enable latched while the clock is low).";
                add(std::move(d));
            } else if (latch) {
                Diagnostic d = mk("CDC-020", Severity::Info, "clock '" + dom.name + "' is gated through a latch that is transparent while the clock is low (clock-gate pattern)",
                                  {}, {}, Confidence::Heuristic);
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"proof_obligation", "every enable path must pass through the latch and the enable must be stable while the clock is high; timing of the latch is not analysed", {}});
                add(std::move(d));
            }
        }
    }
}

// --- CDC-021: a data signal used as an asynchronous set/clear of registers in another clock domain ----------------
void Shared::asyncControls() {
    std::set<std::string> done;
    for (auto& f : m->flops) {
        if (!f.clockResolved) continue;
        for (auto& r : f.resets) {
            if (r.root.kind != RootKind::Logic) continue;
            NodeId q = aliasRoot(r.root.node);
            auto it = m->flopOfQ.find(q);
            if (it == m->flopOfQ.end()) continue;
            const Flop& rf = m->flops[it->second];
            if (!rf.clockResolved) continue;
            bool functional = false;
            std::vector<NodeId> cl;
            std::unordered_set<NodeId> seen{q};
            cl.push_back(q);
            for (size_t i = 0; i < cl.size(); i++)
                for (auto ei : m->out[cl[i]]) {
                    const Edge& e = m->edges[ei];
                    if (e.type == EdgeType::AsyncCtl) continue;
                    if (e.type == EdgeType::Comb && e.exact) { if (seen.insert(e.to).second) cl.push_back(e.to); continue; }
                    functional = true;
                }
            if (!functional) continue;
            Relation rel = relation(rf.domain, f.domain);
            if (rel == Relation::Same) continue;
            std::string name = stripBit(m->nameOf(q));
            if (!done.insert(name + ">" + m->domains[f.domain].name).second) continue;
            evaluated["CDC-021"]++;
            Diagnostic d = mk("CDC-021", rel == Relation::Async ? Severity::Error : rel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                              "signal " + name + " (domain " + m->domains[rf.domain].name + ") is used both as functional data and as an asynchronous set/clear of registers in domain " +
                              m->domains[f.domain].name + " [" + relName(rel) + "]: an asynchronous control crossing a clock domain is unsynchronised",
                              procOf(f).loc, procOf(f).instancePath, rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
            d.signals = {name};
            d.nextStep = "Do not drive asynchronous controls from functional signals; use a dedicated reset (RDC) or a synchronous control.";
            add(std::move(d));
        }
    }
}

// --- CDC-043: rising/falling-edge registers of one clock ---------------------------------------------------------------
void Shared::edgeStructure() {
    for (auto& [names, nodes] : halfCycle_) {
        evaluated["CDC-043"]++;
        const Flop& d = m->flops[m->flopOfQ.at(nodes.second)];
        Diagnostic x = mk("CDC-043", Severity::Warning,
                          "path " + names.first + " -> " + names.second + " connects registers on opposite edges of clock '" + m->domains[d.domain].name +
                          "': it has half a clock period; as part of a synchroniser it halves the settling time", procOf(d).loc, procOf(d).instancePath, Confidence::Heuristic);
        x.signals = {names.first, names.second};
        x.nextStep = "Confirm the half-cycle timing budget, or use the same edge for both registers.";
        add(std::move(x));
    }
    for (auto& f : m->flops)
        if (f.edge == 3 && f.clockResolved) {
            evaluated["CDC-043"]++;
            Diagnostic x = mk("CDC-043", Severity::Warning, "register " + flopName(f) + " is triggered on both clock edges (dual-edge): edge relationships are not modelled",
                              procOf(f).loc, procOf(f).instancePath, Confidence::Unresolved);
            add(std::move(x));
            report->markIncomplete("clock-relationships", "dual-edge register " + flopName(f));
        }
}

} // namespace sverity::cdc

namespace sverity::cdc {

namespace {
struct AnnotatedFinder : slang::ast::ASTVisitor<AnnotatedFinder, slang::ast::VisitFlags::AllGood> {
    std::vector<std::pair<std::string, std::string>> found;   // (hierarchical instance path, definition name)
    void handle(const slang::ast::InstanceSymbol& inst) {
        if (auto* syn = inst.getDefinition().getSyntax(); syn && syn->kind == slang::syntax::SyntaxKind::ModuleDeclaration) {
            auto& md = syn->as<slang::syntax::ModuleDeclarationSyntax>();
            for (auto* ai : md.attributes)
                for (auto* spec : ai->specs) {
                    std::string n = lower(std::string(spec->name.valueText()));
                    if (n == "sverity_sync" || n == "cdc_sync") found.push_back({inst.getHierarchicalPath(), std::string(inst.getDefinition().name)});
                }
        }
        visitDefault(inst);
    }
};
} // namespace

// --- protocol / metadata checks over the crossing groups --------------------------------------------------------------
void Shared::protocols() {
    auto isSyncedSingle = [&](const Group& g) {
        if (g.collapsed || g.dstFlops.size() != 1) return false;
        const Chain& ch = chains_[g.dstFlops[0]];
        return !ch.linkLogic && ch.depth >= 2;
    };
    auto dstLast = [&](const Group& g) { return m->flops[chains_[g.dstFlops[0]].stages.back()].q; };
    auto withTag = [&](const Group& g) {
        Relation rel = relation(g.srcDomain, g.dstDomain);
        return std::string(rel == Relation::Unknown ? " [clock relationship UNKNOWN]" : "");
    };
    auto ev = [&](Diagnostic& d, const Group& g) {
        d.signals = {g.srcName, g.dstName};
        d.evidence.push_back({"launch_register", g.srcName + " (clock domain " + m->domains[g.srcDomain].name + ")", {}});
        d.evidence.push_back({"capture_register", g.dstName + " (clock domain " + m->domains[g.dstDomain].name + ")", {}});
    };
    auto reset041 = [&](const Group& g, const Flop* sf, const std::string& tag) {
        bool exclusive = relation(g.srcDomain, g.dstDomain) == Relation::Exclusive; (void)exclusive;
    if (sf) {
            auto roots = [&](const Flop& x) { std::set<NodeId> r; for (auto& rr : x.resets) r.insert(rr.root.node); return r; };
            std::set<NodeId> rs = roots(*sf), rd = roots(m->flops[chains_[g.dstFlops[0]].stages.back()]);
            if (!rs.empty() && !rd.empty() && rs != rd) {
                evaluated["CDC-041"]++;
                Diagnostic d = mk("CDC-041", Severity::Warning, "crossing " + g.srcName + " -> " + g.dstName + ": the source and the synchroniser are reset by different resets; "
                                  "while one side is in reset the other sees stale or unreleased values" + tag, g.loc, g.hier, Confidence::Heuristic);
                ev(d, g);
                d.proofState = ProofState::NotAttempted;
                d.nextStep = "Reset both sides together or define the release order (see RDC-023).";
                add(std::move(d));
            } else if (!rs.empty() && rd.empty()) {
                evaluated["CDC-041"]++;
                Diagnostic d = mk("CDC-041", Severity::Info, "crossing " + g.srcName + " -> " + g.dstName + ": the source is reset, the synchroniser is not: after a source-only reset the destination keeps "
                                  "the old value for the synchroniser latency" + tag, g.loc, g.hier, Confidence::Heuristic);
                ev(d, g);
                add(std::move(d));
            }
        }
    };
    std::vector<const Group*> single;   // recognised single-bit synchronised crossings
    for (auto& [key, g] : groups_) {
        Relation rel = relation(g.srcDomain, g.dstDomain);
        bool exclusive = rel == Relation::Exclusive;
        const Flop& f0 = m->flops[g.dstFlops[0]];
        std::string tag = withTag(g);

        // CDC-038: SDC exceptions cut the crossing but say nothing about synchronisation
        if (g.srcQ != kNoNode) {
            for (auto* ex : exceptionsFor(g.srcQ, f0.q, true, sdc::PathException::Kind::FalsePath)) {
                evaluated["CDC-038"]++;
                const char* kind = ex->kind == sdc::PathException::Kind::FalsePath ? "set_false_path" : ex->kind == sdc::PathException::Kind::Multicycle ? "set_multicycle_path"
                                 : ex->kind == sdc::PathException::Kind::MaxDelay ? "set_max_delay" : "set_min_delay";
                if (ex->kind == sdc::PathException::Kind::MaxDelay || ex->kind == sdc::PathException::Kind::MinDelay) continue;   // constraints that keep the path timed
                Diagnostic d = mk("CDC-038", Severity::Warning,
                                  std::string("crossing ") + g.srcName + " -> " + g.dstName + " is cut by " + kind + " (" + ex->loc.file + ":" + std::to_string(ex->loc.line) +
                                  "): an exception removes timing checks, it does not make the crossing safe (a synchroniser is " +
                                  (isSyncedSingle(g) || (!g.collapsed && chains_[g.dstFlops[0]].depth >= 2) ? "present" : "NOT recognised") + ")",
                                  g.loc, g.hier, Confidence::Established);
                ev(d, g);
                d.nextStep = "Keep synchroniser/protocol evidence independent of the exception, and justify the exception.";
                add(std::move(d));
            }
        }
        // CDC-037: declared relationship versus observed synchroniser
        if (exclusive && !g.collapsed && chains_[g.dstFlops[0]].depth >= 2) {
            evaluated["CDC-037"]++;
            Diagnostic d = mk("CDC-037", Severity::Warning, "clocks " + m->domains[g.srcDomain].name + " and " + m->domains[g.dstDomain].name +
                              " are declared exclusive but a synchroniser was recognised between them: the constraint and the structure disagree", g.loc, g.hier);
            ev(d, g);
            add(std::move(d));
        }
        // CDC-035: memories crossing domains
        if (g.memory && !g.collapsed) {
            evaluated["CDC-035"]++;
            Diagnostic d = mk("CDC-035", exclusive ? Severity::Info : Severity::Warning,
                              "memory " + g.srcName + " -> " + g.dstName + " is written and read in different clock domains: correctness depends on the pointer/handshake protocol around it" + tag,
                              g.loc, g.hier, Confidence::Unresolved);
            ev(d, g);
            d.proofState = ProofState::NotAttempted;
            d.nextStep = "Guard dual-clock memory access with a recognised async-FIFO pointer scheme; vendor RAM semantics (read-during-write) are not modelled.";
            add(std::move(d));
        }
        if (!g.collapsed && g.dstFlops.size() > 1 && !g.memory && g.srcQ != kNoNode && !chains_[g.dstFlops[0]].linkLogic && chains_[g.dstFlops[0]].depth >= 2)
            reset041(g, m->flopAt(g.srcQ), tag);   // multi-bit synchronised groups get the reset check too
        if (!isSyncedSingle(g) || g.memory) continue;
        single.push_back(&g);
        NodeId sq = g.srcQ, lastQ = dstLast(g);
        const Flop* sf = m->flopAt(sq);

        // CDC-012: synchroniser attributes (profile dependent)
        {
            int marked = 0, total = 0;
            for (auto st : chains_[g.dstFlops[0]].stages) {
                total++;
                auto* sym = m->nodes[m->flops[st].q].sym;
                if (!sym) continue;
                for (auto* a : design_->compilation->getAttributes(*sym)) {
                    std::string n = lower(std::string(a->name));
                    if (n == "async_reg" || n == "sverity_sync" || n == "cdc_sync") { marked++; break; }
                }
            }
            evaluated["CDC-012"]++;
            bool fpga = cfg->profile == "fpga";
            if (marked && marked < total) {
                Diagnostic d = mk("CDC-012", Severity::Warning, "synchroniser " + g.dstName + ": only " + std::to_string(marked) + " of " + std::to_string(total) +
                                  " stages carry the ASYNC_REG/sverity_sync attribute", g.loc, g.hier);
                ev(d, g);
                add(std::move(d));
            } else if (!marked && fpga) {
                Diagnostic d = mk("CDC-012", Severity::Warning, "synchroniser " + g.dstName + " has no ASYNC_REG attribute on its stages (profile fpga): placement and "
                                  "timing tools may not keep the stages together", g.loc, g.hier);
                ev(d, g);
                d.nextStep = "Mark the synchroniser registers with (* ASYNC_REG = \"TRUE\" *).";
                add(std::move(d));
            }
        }

        // CDC-013 / CDC-042: pulses and the clock-rate relation
        if (!exclusive && sf) {
            evaluated["CDC-013"]++; evaluated["CDC-042"]++;
            bool eventLike = false;
            for (auto ei : m->in[sq]) {
                const Edge& e = m->edges[ei];
                if (e.type == EdgeType::Seq && !e.exact && !e.ctrl) eventLike = true;
            }
            double ts = periodNs(g.srcDomain), td = periodNs(g.dstDomain);
            bool fast = ts > 0 && td > 0 && ts < td * 1.5;
            if (ts <= 0 || td <= 0) {   // rates only from create_clock periods: unknown is a WARNING, never "slow enough"
                Diagnostic d = mk("CDC-013", Severity::Warning, "crossing " + g.srcName + " -> " + g.dstName + ": the clock periods are not constrained, so a fast-to-slow pulse loss cannot be excluded" + tag,
                                  g.loc, g.hier, Confidence::Unresolved);
                ev(d, g);
                d.proofState = ProofState::NotAttempted;
                d.nextStep = "Constrain both clocks with create_clock -period.";
                add(std::move(d));
            }
            if (ts > 0 && td > 0 && fast) {
                Diagnostic d = mk("CDC-013", Severity::Warning, "crossing " + g.srcName + " -> " + g.dstName + " goes from a faster clock (" + std::to_string(ts) +
                                  " ns) to a slower or comparable one (" + std::to_string(td) + " ns): a pulse or short level can be missed" + tag, g.loc, g.hier);
                ev(d, g);
                d.proofState = ProofState::NotAttempted;
                d.nextStep = "Hold the signal for at least two destination periods, or use a toggle/handshake synchroniser.";
                add(std::move(d));
            } else if (eventLike) {
                bool unknownRate = ts <= 0 || td <= 0;
                Diagnostic d = mk("CDC-042", unknownRate ? Severity::Warning : Severity::Info,
                                  "crossing " + g.srcName + " -> " + g.dstName + " carries a computed (event-like) signal: nothing guarantees it is held long enough for the destination" +
                                  (unknownRate ? " (clock periods unknown)" : "") + tag, g.loc, g.hier,
                                  unknownRate ? Confidence::Unresolved : Confidence::Heuristic);
                ev(d, g);
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"proof_obligation", "minimum pulse width >= 1.5 x destination period (or a toggle/handshake protocol) is not verified", {}});
                add(std::move(d));
            }
        }

        // CDC-014: toggle (event) synchroniser
        if (sf && m->selfLogic.count(sq)) {
            evaluated["CDC-014"]++;
            bool detector = chains_[g.dstFlops[0]].edgeDetector;
            if (g.anyLogicBefore) {
                Diagnostic d = mk("CDC-014", Severity::Warning, "toggle register " + g.srcName + " is not launched directly: logic between the toggle flop and the synchroniser can glitch" + tag, g.loc, g.hier);
                ev(d, g);
                add(std::move(d));
            }
            if (!detector) {
                Diagnostic d = mk("CDC-014", Severity::Warning, "toggle register " + g.srcName + " is synchronised to " + g.dstName +
                                  " but no destination edge detector (XOR with a delayed copy) was found: the receiver sees a level, not an event" + tag,
                                  g.loc, g.hier, Confidence::Heuristic);
                ev(d, g);
                d.nextStep = "Add a delayed copy of the synchronised toggle and XOR it with the synchroniser output.";
                add(std::move(d));
            } else {
                Diagnostic d = mk("CDC-014", Severity::Info, "toggle synchroniser recognised for " + g.srcName + " -> " + g.dstName + tag, g.loc, g.hier, Confidence::Heuristic);
                ev(d, g);
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"proof_obligation", "toggles must be at least two destination clock periods apart, otherwise events are lost", {}});
                add(std::move(d));
                evaluated["CDC-024"]++;
                Diagnostic o = mk("CDC-024", Severity::Info, "functional correctness of the toggle protocol " + g.srcName + " -> " + g.dstName + " is not verified", g.loc, g.hier, Confidence::Heuristic);
                o.proofState = ProofState::NotAttempted;
                add(std::move(o));
            }
        }

        reset041(g, sf, tag);
    }

    // CDC-015: request/acknowledge loops between two synchronised single-bit crossings in opposite directions
    auto coneHas = [&](NodeId flopQ, const Group& g) {   // does the register's cone contain any stage of g's synchroniser?
        auto it = m->flopOfQ.find(flopQ);
        if (it == m->flopOfQ.end()) return false;
        Cone c = coneOf(m->flops[it->second]);
        for (auto& s : c.flops)
            for (auto st : chains_[g.dstFlops[0]].stages) if (m->flops[st].q == s.q) return true;
        return false;
    };
    for (auto* a : single)
        for (auto* b : single) {
            if (a == b || a->srcDomain != b->dstDomain || a->dstDomain != b->srcDomain || a->hier != b->hier || a->srcName >= b->srcName) continue;
            evaluated["CDC-015"]++;
            bool ackFollowsReq = coneHas(b->srcQ, *a), reqFollowsAck = coneHas(a->srcQ, *b);
            if (ackFollowsReq && reqFollowsAck) {
                Diagnostic d = mk("CDC-015", Severity::Info, "request/acknowledge loop recognised: " + a->srcName + " (" + m->domains[a->srcDomain].name + ") <-> " + b->srcName +
                                  " (" + m->domains[b->srcDomain].name + ")", a->loc, a->hier, Confidence::Heuristic);
                d.signals = {a->srcName, b->srcName};
                d.proofState = ProofState::NotAttempted;
                d.evidence.push_back({"proof_obligation", "the 4-phase protocol (req held until ack, ack held until req drops, data stable while req is high) is not verified", {}});
                add(std::move(d));
                evaluated["CDC-024"]++;
                Diagnostic o = mk("CDC-024", Severity::Info, "functional correctness of the handshake " + a->srcName + " <-> " + b->srcName + " is not verified", a->loc, a->hier, Confidence::Heuristic);
                o.proofState = ProofState::NotAttempted;
                add(std::move(o));
            } else if (ackFollowsReq != reqFollowsAck) {
                const Group* req = ackFollowsReq ? a : b;
                Diagnostic d = mk("CDC-015", Severity::Warning, "possible request/acknowledge mismatch: " + (ackFollowsReq ? a->srcName : b->srcName) +
                                  " is acknowledged by " + (ackFollowsReq ? b->srcName : a->srcName) + ", but the request does not depend on the acknowledge (it is never deasserted by it)",
                                  req->loc, req->hier, Confidence::Heuristic);
                d.signals = {a->srcName, b->srcName};
                d.proofState = ProofState::NotAttempted;
                add(std::move(d));
            }
        }

    // CDC-034 / CDC-024: asynchronous-FIFO pointer pairs (Gray pointers in opposite directions) and their resets
    for (auto& [k1, g1] : groups_)
        for (auto& [k2, g2] : groups_) {
            if (&g1 >= &g2 || !g1.gray || !g2.gray || g1.srcDomain != g2.dstDomain || g1.dstDomain != g2.srcDomain || g1.hier != g2.hier) continue;
            evaluated["CDC-034"]++; evaluated["CDC-024"]++;
            auto roots = [&](const Group& g) {
                std::set<NodeId> r; bool unreset = false;
                for (auto q : g.srcBits) { const Flop* f = m->flopAt(q); if (!f) continue; if (f->resets.empty()) unreset = true; for (auto& rr : f->resets) r.insert(rr.root.node); }
                return std::make_pair(r, unreset);
            };
            auto [r1, u1] = roots(g1);
            auto [r2, u2] = roots(g2);
            std::string msg = "asynchronous FIFO pointers " + g1.srcName + " (" + m->domains[g1.srcDomain].name + ") <-> " + g2.srcName + " (" + m->domains[g2.srcDomain].name + "): ";
            if (u1 || u2) msg += "a pointer register has no asynchronous reset; ";
            if (r1 != r2 && !r1.empty() && !r2.empty()) msg += "the two pointers are reset by different resets, so reset release is not coordinated across the domains; ";
            bool bad = u1 || u2 || (r1 != r2 && !r1.empty() && !r2.empty());
            Diagnostic d = mk("CDC-034", bad ? Severity::Warning : Severity::Info, bad ? msg + "pointer values can be inconsistent after reset" : msg + "pointer pair recognised, both reset by the same reset",
                              g1.loc, g1.hier, Confidence::Heuristic);
            d.signals = {g1.srcName, g2.srcName};
            d.proofState = ProofState::NotAttempted;
            d.evidence.push_back({"proof_obligation", "each domain must see the other's pointer only after a coordinated reset release (see RDC-023); empty/full flag correctness is not verified", {}});
            add(std::move(d));
            Diagnostic o = mk("CDC-024", Severity::Info, "functional correctness of the asynchronous FIFO " + g1.srcName + " / " + g2.srcName + " (pointer arithmetic, full/empty, data path) is not verified",
                              g1.loc, g1.hier, Confidence::Heuristic);
            o.proofState = ProofState::NotAttempted;
            add(std::move(o));
        }

    // RDC-023: reset release sequencing between the two sides of a recognised FIFO / handshake
    for (auto& [k1, g1] : groups_)
        for (auto& [k2, g2] : groups_) {
            if (&g1 >= &g2 || !g1.gray || !g2.gray || g1.srcDomain != g2.dstDomain || g1.dstDomain != g2.srcDomain || g1.hier != g2.hier) continue;
            evaluated["RDC-023"]++;
            auto resetsOf = [&](const Group& g) { std::set<NodeId> r; for (auto q : g.srcBits) if (auto* f = m->flopAt(q)) for (auto& rr : f->resets) r.insert(rr.root.node); return r; };
            std::set<NodeId> ra = resetsOf(g1), rb = resetsOf(g2);
            bool sameReset = !ra.empty() && ra == rb;
            Diagnostic d = mk("RDC-023", sameReset ? Severity::Info : Severity::Warning, "FIFO " + g1.srcName + " / " + g2.srcName + ": the order in which the two clock domains leave reset is not defined by the design; "
                              "a side that leaves reset early can read or write before the other side's pointers are valid" +
                              std::string(sameReset ? " (both pointers share one reset, but each domain still releases it on its own clock)" : " (the pointers are reset independently)"),
                              g1.loc, g1.hier, Confidence::Unresolved);
            d.signals = {g1.srcName, g2.srcName};
            d.proofState = ProofState::NotAttempted;
            d.evidence.push_back({"proof_obligation", "reset release of both domains must be sequenced (or each side must ignore the other until a synchronised ready); not verified", {}});
            add(std::move(d));
        }
    for (auto* a : single)
        for (auto* b : single) {
            if (a == b || a->srcDomain != b->dstDomain || a->dstDomain != b->srcDomain || a->hier != b->hier || a->srcName >= b->srcName) continue;
            bool hs = coneHas(b->srcQ, *a) && coneHas(a->srcQ, *b);
            if (!hs) continue;
            evaluated["RDC-023"]++;
            Diagnostic d = mk("RDC-023", Severity::Warning, "handshake " + a->srcName + " <-> " + b->srcName + ": the reset release order of its two domains is not defined; a request issued "
                              "right after one side leaves reset can be lost or acknowledged spuriously", a->loc, a->hier, Confidence::Unresolved);
            d.signals = {a->srcName, b->srcName};
            d.proofState = ProofState::NotAttempted;
            add(std::move(d));
        }
    // RDC-024: a synchronised control used as an asynchronous reset in the receiving domain (reset request / acknowledge)
    for (auto& [q, so] : syncedOut_) {
        bool asReset = false;
        std::vector<NodeId> cl{q};
        std::unordered_set<NodeId> seen{q};
        for (size_t i = 0; i < cl.size(); i++)
            for (auto ei : m->out[cl[i]]) {
                const Edge& e = m->edges[ei];
                if (e.type == EdgeType::AsyncCtl) asReset = true;
                else if (e.type == EdgeType::Comb && seen.insert(e.to).second) cl.push_back(e.to);
            }
        if (!asReset) continue;
        evaluated["RDC-024"]++;
        Diagnostic d = mk("RDC-024", Severity::Warning, "control " + so.srcName + " (domain " + m->domains[so.srcDomain].name + ") is synchronised into domain " + m->domains[so.dstDomain].name +
                          " and used there as an asynchronous reset: a reset request/acknowledge protocol across domains; sequencing and completion are not verified", {}, so.hier, Confidence::Heuristic);
        d.signals = {so.srcName};
        d.proofState = ProofState::NotAttempted;
        d.evidence.push_back({"proof_obligation", "the requester must hold the request until the acknowledge from the reset domain returns; the reset must not release before its users are quiescent", {}});
        add(std::move(d));
    }

    // CDC-032: the bits of one source bus captured into separate registers must see the same synchroniser structure
    {
        struct Bus { std::set<int> depths; uint64_t bits = 0; const Group* first = nullptr; };
        std::map<std::tuple<std::string, uint32_t, uint32_t, std::string>, Bus> buses;
        for (auto& [key, g] : groups_) {
            if (g.collapsed) continue;
            auto& b = buses[{g.srcName, g.srcDomain, g.dstDomain, g.hier}];
            if (!b.first) b.first = &g;
            for (auto di : g.dstFlops) { b.depths.insert(chains_[di].linkLogic ? 1 : chains_[di].depth); b.bits++; }
        }
        // bits of one source bus (or one destination vector) synchronised through separate registers: independent synchronisation
        std::set<std::string> busDone;
        auto busWarn = [&](const Group& g, const std::string& what, uint64_t bits) {
            Relation rel = relation(g.srcDomain, g.dstDomain);
            if (rel == Relation::Same || g.gray || g.qualified) return;
            if (!busDone.insert(what).second) return;
            evaluated["CDC-009"] += bits;
            Diagnostic d = mk("CDC-009", rel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                              std::to_string(bits) + " bits of " + what + " are synchronised independently (separate registers/instances): bit skew can capture an inconsistent value",
                              g.loc, g.hier, rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
            d.signals = {g.srcName, g.dstName};
            d.proofState = ProofState::NotAttempted;
            d.nextStep = "Prove the bus is Gray-coded / one-hot / held stable by a synchronised qualifier, or use a handshake or async FIFO.";
            add(std::move(d));
        };
        {
            std::map<std::tuple<std::string, uint32_t, uint32_t, std::string>, std::pair<uint64_t, const Group*>> byDst, bySrc;
            for (auto& [key, g] : groups_) {
                if (g.collapsed) continue;
                bool allSynced = true;
                for (auto di : g.dstFlops) allSynced = allSynced && !chains_[di].linkLogic && chains_[di].depth >= 2;
                if (!allSynced) continue;
                auto& a = byDst[{g.dstName, g.srcDomain, g.dstDomain, g.hier}]; a.first += g.dstFlops.size(); if (!a.second) a.second = &g;
                auto& b = bySrc[{g.srcName, g.srcDomain, g.dstDomain, g.hier}]; b.first += g.dstFlops.size(); if (!b.second) b.second = &g;
            }
            // a bus split over several groups (several source symbols into one vector, or one source into several registers)
            for (auto& [k, v] : byDst) if (v.first >= 2 && groups_.size() > 1) {
                uint64_t srcSyms = 0;
                for (auto& [key, g] : groups_) if (g.dstName == std::get<0>(k) && g.srcDomain == std::get<1>(k) && g.dstDomain == std::get<2>(k)) srcSyms++;
                if (srcSyms >= 2) busWarn(*v.second, std::get<0>(k), v.first);
            }
            for (auto& [k, v] : bySrc) if (v.first >= 2 && v.second->dstFlops.size() == 1) {
                uint64_t dsts = 0;
                for (auto& [key, g] : groups_) if (g.srcName == std::get<0>(k) && g.srcDomain == std::get<1>(k) && g.dstDomain == std::get<2>(k)) dsts++;
                if (dsts >= 2) busWarn(*v.second, std::get<0>(k), v.first);
            }
        }
        for (auto& [k, b] : buses) {
            if (b.bits < 2 || b.depths.size() < 2) continue;
            const Group& g = *b.first;
            if (g.dstFlops.size() > 1) continue;   // a single destination vector is judged inside the crossing analysis
            evaluated["CDC-032"] += b.bits;
            Relation rel = relation(g.srcDomain, g.dstDomain);
            std::string ds;
            for (int d : b.depths) ds += (ds.empty() ? "" : "/") + std::to_string(d);
            Diagnostic d = mk("CDC-032", rel == Relation::Async ? Severity::Error : rel == Relation::Exclusive ? Severity::Info : Severity::Warning,
                              "the " + std::to_string(b.bits) + " bits of " + g.srcName + " are captured with different synchroniser depths (" + ds + " stages): the bundle is not coherent in the destination domain",
                              g.loc, g.hier, rel == Relation::Unknown ? Confidence::Unresolved : Confidence::Established);
            d.signals = {g.srcName};
            d.nextStep = "Give every bit of a bundle the same synchroniser structure, or cross it with a qualifier/handshake.";
            add(std::move(d));
        }
    }

    // CDC-037: distinct clock roots that the constraints declare to be one clock (same SDC name): crossings between them are
    // treated as synchronous; never silent
    for (auto& [a, b] : sameNameDomainPairs_) {
        evaluated["CDC-037"]++;
        Diagnostic d = mk("CDC-037", Severity::Warning, "clock roots '" + m->domains[a].name + "' and '" + m->domains[b].name + "' are declared as the same SDC clock '" + domainClockName(a) +
                          "': register-to-register paths between them are treated as synchronous, no crossing is analysed",
                          {}, {}, Confidence::Unresolved);
        d.nextStep = "Use distinct clock names unless the two ports really carry the same clock; declare their relation with set_clock_groups.";
        add(std::move(d));
        report->markIncomplete("clock-relationships", "distinct clock roots share one SDC clock name: " + m->domains[a].name + ", " + m->domains[b].name);
    }

    // CDC-040: library cells declared as synchronisers (--library)
    for (auto& li : m->libInstances) {
        evaluated["CDC-040"]++;
        Diagnostic d = mk("CDC-040", Severity::Info, "library synchroniser cell '" + li.module + "' (" + std::to_string(li.stages) + " stages) at " + li.hierarchy +
                          " is recognised through the library description, not by its body", li.loc, li.hierarchy, Confidence::Heuristic);
        d.proofState = ProofState::NotAttempted;
        d.evidence.push_back({"trust_model", "the library declaration is trusted: the cell's real stage count, clocking and metastability behaviour are not verified", {}});
        add(std::move(d));
    }

    // CDC-040: modules the user annotated as synchronisers must contain a recognised chain
    if (design_ && design_->compilation) {
        AnnotatedFinder af;
        design_->compilation->getRoot().visit(af);
        for (auto& [path, def] : af.found) {
            evaluated["CDC-040"]++;
            bool recognised = false;
            for (auto& [q, so] : syncedOut_) if (so.hier == path || so.hier.rfind(path + ".", 0) == 0) recognised = true;
            Diagnostic d = mk("CDC-040", recognised ? Severity::Info : Severity::Warning,
                              recognised ? "user-defined synchroniser module '" + def + "' (" + path + ") is annotated and a synchroniser structure was recognised inside it"
                                         : "module '" + def + "' (" + path + ") is annotated as a synchroniser but no synchroniser structure (>= 2 same-clock register stages fed from another domain) was recognised: "
                                           "the annotation contradicts the structure",
                              {}, path, recognised ? Confidence::Heuristic : Confidence::Established);
            if (recognised) d.proofState = ProofState::NotAttempted;
            add(std::move(d));
        }
    }
}

} // namespace sverity::cdc
