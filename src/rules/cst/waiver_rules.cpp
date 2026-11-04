// CST-006 / CST-022: waiver hygiene is enforced by the waiver subsystem (src/report/waiver.cpp),
// which runs after findings are finalized. These objects register the rules so coverage is
// accounted for; the analyzer fills in objectsEvaluated with the number of waivers examined.
#include "rules/rules.h"

namespace sverity {
namespace {
class WaiverRule final : public Rule {
public:
    explicit WaiverRule(const char* id) : id_(id) {}
    std::string_view id() const override { return id_; }
    void run(RuleContext&) override {}
private:
    std::string id_;
};
// CST-019: the engine emits the accounting after every rule has run; this registers what that covers.
class RuleAccounting final : public Rule {
public:
    std::string_view id() const override { return "CST-019"; }
    void run(RuleContext&) override {}
    RuleState completeness() const override { return RuleState::Partial; }
    std::string partialReason() const override { return "accounts for rules not fully evaluated and for unknown rule names; profile policy drift is not checked"; }
};
} // namespace

std::vector<std::unique_ptr<Rule>> makeWaiverRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<WaiverRule>("CST-006"));
    v.push_back(std::make_unique<WaiverRule>("CST-022"));
    v.push_back(std::make_unique<RuleAccounting>());
    return v;
}
} // namespace sverity
