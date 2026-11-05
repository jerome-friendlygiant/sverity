// Reset-domain analysis on the shared model: where each asynchronous reset comes from, whether its release is
// synchronised to the clock of the registers it resets, and whether it is generated in another clock domain.
//
// Policy (same as CDC): a reset whose release cannot be shown to be synchronised is a WARNING (unresolved), a reset
// generated in a clock domain declared asynchronous to its users is an ERROR, a recognised reset synchroniser is INFO
// with its trust model. Nothing is reported as safe.
#include "rules/rdc/rdc_shared.h"
#include "rules/rules.h"

namespace sverity {
namespace {
using rdc::RdcShared;

class RdcRule final : public Rule {
public:
    RdcRule(std::string id, std::shared_ptr<RdcShared> sh, std::string gap) : id_(std::move(id)), sh_(std::move(sh)), gap_(std::move(gap)) {}
    std::string_view id() const override { return id_; }
    RuleState completeness() const override { return gap_.empty() ? RuleState::Executed : RuleState::Partial; }
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
    v.push_back(std::make_unique<RdcRule>("RDC-003", sh, ""));
    v.push_back(std::make_unique<RdcRule>("RDC-006", sh, ""));
    v.push_back(std::make_unique<RdcRule>("RDC-007", sh, ""));
    auto add = [&](const char* id, const char* gap) { v.push_back(std::make_unique<RdcRule>(id, sh, gap)); };
    add("RDC-002", "");
    add("RDC-004", "");
    add("RDC-005", "");
    add("RDC-008", "");
    add("RDC-009", "");
    add("RDC-010", "");
    add("RDC-011", "");
    add("RDC-012", "");
    add("RDC-013", "");
    add("RDC-014", "");
    add("RDC-016", "");
    add("RDC-017", "");
    add("RDC-019", "");
    add("RDC-020", "");
    add("RDC-025", "");
    add("RDC-026", "");
    add("RDC-027", "");
    add("RDC-028", "");
    add("RDC-030", "");
    return v;
}

} // namespace sverity
