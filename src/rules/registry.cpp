#include "rules/rules.h"

namespace sverity {

namespace {
std::vector<RuleFactory>& customFactories() {
    static std::vector<RuleFactory> f;
    return f;
}
} // namespace

void registerRule(RuleFactory factory) { customFactories().push_back(std::move(factory)); }
void clearRegisteredRules() { customFactories().clear(); }

std::vector<std::unique_ptr<Rule>> makeAllRules() {
    std::vector<std::unique_ptr<Rule>> all;
    auto append = [&](std::vector<std::unique_ptr<Rule>> v) {
        for (auto& r : v) all.push_back(std::move(r));
    };
    append(makeSlangBackedRules());
    append(makeWaiverRules());
    append(makeSyntaxLintRules());
    append(makeLintProceduralRules());
    append(makeLintFlowRules());
    append(makeLintSynthRules());
    append(makeLintWidthRules());
    append(makeLintStructRules());
    append(makeLintModelRules());
    append(makeLintDataflowRules());
    append(makeGraphLintRules());
    append(makeCdcRules());
    append(makeRdcRules());
    for (auto& f : customFactories()) all.push_back(f());
    return all;
}

} // namespace sverity
