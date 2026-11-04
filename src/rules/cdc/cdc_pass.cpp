// Shared CDC / reset / constraint analysis over the design model (RFC section 2: "shared analysis pass").
// One lazily-built analysis feeds all rule objects of this family; no rule re-parses RTL.
//
// Honesty policy implemented here:
//  * unknown clock relationship          -> WARNING, confidence "unresolved" (never "safe")
//  * recognised 2-flop synchroniser      -> INFO with the trust-model assumptions spelled out
//  * heuristic classifications           -> confidence "heuristic"
//  * every graph-precision fallback      -> CST-010 WARNING + scope INCOMPLETE
#include "rules/cdc/cdc_shared.h"
#include "rules/rules.h"

namespace sverity {
namespace {
using cdc::Shared;

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
    add("CDC-001", "");
    add("CDC-002", "");
    add("CDC-003", "");
    add("CDC-004", "");
    add("CDC-005", "");
    add("CDC-006", "");
    add("CDC-007", "");
    add("CDC-008", "");
    add("CDC-009", "");
    add("CDC-016", "");
    add("CDC-017", "");
    add("CDC-018", "");
    add("CDC-028", "");
    add("CDC-032", "");
    add("CDC-039", "");
    add("CDC-010", "");
    add("CDC-012", "checks RTL synchronizer attributes only; recognized primitive metadata and technology profile bundles are not checked");
    add("CDC-013", "");
    add("CDC-014", "");
    add("CDC-015", "");
    add("CDC-019", "");
    add("CDC-020", "");
    add("CDC-045", "");
    add("CDC-046", "");
    add("CDC-023", "");
    add("CDC-024", "");
    add("CDC-026", "");
    add("CDC-030", "");
    add("CDC-031", "");
    add("CDC-034", "");
    add("CDC-035", "");
    add("CDC-037", "");
    add("CDC-038", "");
    add("CDC-040", "");
    add("CDC-041", "");
    add("CDC-043", "");
    add("CDC-044", "");
    add("RDC-023", "");
    add("RDC-024", "");
    add("CDC-029", "");
    add("CST-001", "reset classification from event lists and a heuristic synchronous-reset pattern; explicit user-declared no_reset is not supported yet");
    add("CST-002", "only create_clock source objects are resolved");
    add("CST-004", "relationship derived from SDC clock names and set_clock_groups only; generated-clock and exception semantics are not applied");
    add("CST-005", "only modules that slang reports as unknown are inventoried; encrypted/blackbox-attributed modules are not distinguished");
    add("CST-009", "");
    add("CST-010", "records only the fallbacks produced by the connectivity/clock/reset model and in-source slang pragmas; constructs the model ignores without a fallback (classes, tasks, initial blocks, primitives) are not enumerated");
    add("CST-011", "clock constraints only; reset and I/O constraint coverage not evaluated");
    add("CST-012", "Input-side and abstract-IP CDC contracts are not checked; output-boundary contracts only.");
    add("CST-014", "Cross-mode validation and mutually exclusive clock configuration validation are not implemented.");
    add("CST-018", "parse problems and unreadable files only; semantic validation of option values is limited");
    add("RDC-001", "");
    return v;
}

} // namespace sverity
