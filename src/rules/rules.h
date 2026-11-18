// Private factory declarations for rule families. Add one factory per family/pass here.
#pragma once
#include <memory>
#include <vector>

#include "sverity/rule.h"

namespace sverity {
std::vector<std::unique_ptr<Rule>> makeSlangBackedRules();   // rules/lint/slang_backed.cpp
/// Demotes the slang width and sign findings of one rule (LNT-004 or LNT-005) to INFO where a value proof holds (rules/lint/value_proofs.cpp).
void demoteProvenFindings(RuleContext& ctx, const std::string& ruleId);
/// Demotes slang's case-default findings (LNT-065) to INFO on case statements whose items match every two-state selector value (rules/lint/lint_struct.cpp).
void demoteCompleteCaseDefaults(RuleContext& ctx);
/// After every rule has run: findings inside a translate_off region that LNT-083 found to hold only a simulation checker become INFO
/// (rules/lint/lint_pragma.cpp). Never drops one.
void demoteSimulationRegionFindings(Design& design, Report& report);
std::vector<std::unique_ptr<Rule>> makeWaiverRules();        // rules/cst/waiver_rules.cpp
// Native AST lint, one factory per lint_<topic>.cpp (a new topic = a new file + one line here and in registry.cpp)
std::vector<std::unique_ptr<Rule>> makeLintProceduralRules();
std::vector<std::unique_ptr<Rule>> makeLintFlowRules();
std::vector<std::unique_ptr<Rule>> makeLintSynthRules();
std::vector<std::unique_ptr<Rule>> makeLintPragmaRules();   // rules/lint/lint_pragma.cpp
std::vector<std::unique_ptr<Rule>> makeLintWidthRules();
std::vector<std::unique_ptr<Rule>> makeLintStructRules();
std::vector<std::unique_ptr<Rule>> makeLintModelRules();
std::vector<std::unique_ptr<Rule>> makeLintDataflowRules();   // rules/lint/lint_dataflow.cpp: rules on the flow analysis (src/analysis)
std::vector<std::unique_ptr<Rule>> makeGraphLintRules();     // rules/lint/graph_lint.cpp
std::vector<std::unique_ptr<Rule>> makeRdcRules();           // rules/rdc/rdc_pass.cpp
std::vector<std::unique_ptr<Rule>> makeCdcRules();           // rules/cdc/cdc_pass.cpp (CDC/RDC/CST model-based)
std::vector<std::unique_ptr<Rule>> makeSyntaxLintRules();         // rules/lint/lint_syntax.cpp
} // namespace sverity
