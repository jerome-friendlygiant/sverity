// Private factory declarations for rule families. Add one factory per family/pass here.
#pragma once
#include <memory>
#include <vector>

#include "sverity/rule.h"

namespace sverity {
std::vector<std::unique_ptr<Rule>> makeSlangBackedRules();   // rules/lint/slang_backed.cpp
std::vector<std::unique_ptr<Rule>> makeWaiverRules();        // rules/cst/waiver_rules.cpp
std::vector<std::unique_ptr<Rule>> makeGraphLintRules();     // rules/lint/graph_lint.cpp
std::vector<std::unique_ptr<Rule>> makeRdcRules();           // rules/rdc/rdc_pass.cpp
std::vector<std::unique_ptr<Rule>> makeCdcRules();           // rules/cdc/cdc_pass.cpp (CDC/RDC/CST model-based)
std::vector<std::unique_ptr<Rule>> makeStyleRules();         // rules/style/style_rules.cpp
} // namespace sverity
