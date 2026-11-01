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
} // namespace

std::vector<std::unique_ptr<Rule>> makeWaiverRules() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<WaiverRule>("CST-006"));
    v.push_back(std::make_unique<WaiverRule>("CST-022"));
    return v;
}
} // namespace sverity
