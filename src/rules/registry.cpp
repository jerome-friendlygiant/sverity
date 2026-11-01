#include "rules/rules.h"

namespace sverity {

std::vector<std::unique_ptr<Rule>> makeAllRules() {
    std::vector<std::unique_ptr<Rule>> all;
    auto append = [&](std::vector<std::unique_ptr<Rule>> v) {
        for (auto& r : v) all.push_back(std::move(r));
    };
    append(makeSlangBackedRules());
    append(makeWaiverRules());
    append(makeStyleRules());
    append(makeGraphLintRules());
    append(makeCdcRules());
    append(makeRdcRules());
    return all;
}

} // namespace sverity
