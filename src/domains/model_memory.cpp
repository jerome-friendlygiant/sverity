// Approximate heap bytes of the model's structures, for --timings. Sizes times element sizes, using capacity, never a walk of the
// allocator: cheap, and good to a few percent (an allocation also carries about 16 bytes of allocator overhead, which is added per
// element of a node-based container and per heap string, not per vector).
#include "domains/model.h"

#include <string>

namespace sverity::model {
namespace {

constexpr uint64_t kNode = 16;   // allocator header and rounding of one hash-container node or heap string

uint64_t heap(const std::string& s) { return s.capacity() > 15 ? s.capacity() + 1 + kNode : 0; }
uint64_t heap(const SourceLocation& l) { return heap(l.file); }
template <class T> uint64_t flat(const std::vector<T>& v) { return v.capacity() * sizeof(T) + (v.capacity() ? kNode : 0); }
template <class M> uint64_t table(const M& m) { return m.bucket_count() * sizeof(void*) + m.size() * (sizeof(typename M::value_type) + sizeof(void*) + kNode); }

} // namespace

size_t Adjacency::bytes() const { return (offsets_.capacity() + data_.capacity()) * sizeof(uint32_t); }

std::vector<std::pair<std::string, uint64_t>> modelMemory(const Model& m) {
    std::vector<std::pair<std::string, uint64_t>> out;
    auto add = [&](const char* name, uint64_t bytes) { out.emplace_back(std::string("bytes_") + name, bytes); };
    add("nodes", m.nodes.capacity() * sizeof(Node));
    add("edges", m.edges.capacity() * sizeof(Edge));
    add("adjacency_in_out", m.in.bytes() + m.out.bytes());
    uint64_t procs = m.procs.capacity() * sizeof(ProcInfo);
    for (const auto& p : m.procs) procs += heap(p.loc) + heap(p.instancePath);
    add("procedures", procs);
    uint64_t flops = m.flops.capacity() * sizeof(Flop);
    for (const auto& f : m.flops) flops += flat(f.resets) + flat(f.resetKinds) + flat(f.resetValues) + flat(f.otherClockSignals) + flat(f.otherDomains);
    add("flops", flops);
    add("flop_index", table(m.flopOfQ));
    uint64_t writes = m.proceduralWrites.capacity() * sizeof(ProceduralWrite) + m.copyBackWrites.capacity() * sizeof(ProceduralWrite);
    for (const auto* v : {&m.proceduralWrites, &m.copyBackWrites})
        for (const auto& w : *v) writes += heap(w.loc) + heap(w.reason) + flat(w.mayWrite) + flat(w.scopes);
    add("procedural_writes", writes);
    add("procedural_writes_index", table(m.writesByProcess_));
    uint64_t reads = m.processReads.capacity() * sizeof(ProcessRead) + m.systemWrites.capacity() * sizeof(SystemWrite);
    for (const auto& r : m.processReads) reads += heap(r.loc) + heap(r.reason);
    for (const auto& w : m.systemWrites) reads += heap(w.loc) + heap(w.name);
    add("process_reads_system_writes", reads);
    uint64_t sites = m.conditionSites.capacity() * sizeof(ConditionSite);
    for (const auto& c : m.conditionSites) sites += heap(c.loc) + flat(c.reads) + heap(c.call);
    add("condition_sites", sites);
    uint64_t guards = table(m.guards), qguards = table(m.qualifierGuards), cguards = table(m.combQualifierDrivers), bools = table(m.booleanDrivers);
    for (const auto& [n, lists] : m.guards) { guards += flat(lists); for (const auto& l : lists) guards += flat(l); }
    std::unordered_set<const std::vector<NodeId>*> countedDependencies;
    std::unordered_set<const std::vector<GuardCondition>*> countedGuards;
    for (const auto& [n, lists] : m.qualifierGuards) {
        qguards += flat(lists);
        for (const auto& write : lists) {
            if (countedGuards.insert(write.guards.get()).second) {
                qguards += sizeof(*write.guards) + flat(*write.guards);
                for (const auto& g : *write.guards) qguards += flat(g.dependencies);
            }
            for (const auto& deps : {write.dataDependencies.get(), write.controlDependencies.get()})
                if (deps && countedDependencies.insert(deps).second) qguards += sizeof(*deps) + flat(*deps);
        }   // predicate trees are shared and not counted
    }
    for (const auto& [n, summary] : m.combQualifierDrivers) {
        cguards += flat(summary.arms);
        for (const auto& arm : summary.arms) {
            cguards += flat(arm.guards);
            for (const auto& deps : {arm.dataDependencies.get(), arm.controlDependencies.get()})
                if (deps && countedDependencies.insert(deps).second) cguards += sizeof(*deps) + flat(*deps);
            for (const auto& g : arm.guards) cguards += flat(g.dependencies);
        }
    }
    for (const auto& [n, v] : m.booleanDrivers) bools += flat(v);
    add("guards", guards);
    add("qualifier_guards_shared_predicates_excluded", qguards + cguards);
    add("boolean_drivers_shared_values_excluded", bools);
    uint64_t consts = table(m.constDrivers) + table(m.uncertainCallDrivers) + table(m.modeConstants) + table(m.stuckFlopValues);
    for (const auto& [n, v] : m.constDrivers) consts += flat(v);
    for (const auto& [n, v] : m.uncertainCallDrivers) consts += flat(v);
    add("const_and_call_drivers", consts);
    add("node_flag_vectors", m.blackBoxDriven.capacity() + m.topInput.capacity() + m.unresolvedDriven.capacity() + m.topOutput.capacity());
    add("node_sets", table(m.selfComb) + table(m.selfCombProven) + table(m.selfCombApproximate) + table(m.combSelfRewritten) + table(m.selfExact) + table(m.multiSequential) +
                         table(m.selfLogic) + table(m.loopVariables) + table(m.procedureTemps));
    uint64_t fallbacks = m.fallbacks.capacity() * sizeof(Fallback) + m.callUncertainties.capacity() * sizeof(CallUncertainty);
    for (const auto& f : m.fallbacks) fallbacks += heap(f.category) + heap(f.detail) + heap(f.loc);
    for (const auto& c : m.callUncertainties) fallbacks += heap(c.call) + heap(c.reason) + heap(c.location);
    add("fallbacks", fallbacks);
    uint64_t domains = m.domains.capacity() * sizeof(Domain);
    for (const auto& d : m.domains) domains += heap(d.name);
    add("domains", domains);
    return out;
}

} // namespace sverity::model
