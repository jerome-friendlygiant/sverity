#include <map>
#include <string>

#include "sverity/frontend.h"

namespace sverity {

// slang warning option name -> SVerity catalog rule. Provisional: severity policy per rule is
// still "needs_specification"; slang's own severity is preserved verbatim alongside.
const std::map<std::string, std::string>& slangRuleTable() {
    static const std::map<std::string, std::string> table = {
        {"width-trunc", "LNT-004"}, {"width-expand", "LNT-004"}, {"port-width-trunc", "LNT-004"},
        {"port-width-expand", "LNT-004"}, {"implicit-conv", "LNT-004"}, {"packed-array-conv", "LNT-004"},
        {"sign-conversion", "LNT-005"}, {"sign-compare", "LNT-005"}, {"ineffective-sign", "LNT-005"},
        {"index-oob", "LNT-006"}, {"range-oob", "LNT-006"}, {"range-width-oob", "LNT-006"},
        {"implicit-net", "LNT-007"}, {"implicit-net-port", "LNT-007"},
        {"undriven-net", "LNT-008"}, {"undriven-port", "LNT-008"}, {"unassigned-variable", "LNT-008"},
        {"multiple-cont-assigns", "LNT-009"}, {"multiple-always-assigns", "LNT-009"},
        {"mixed-var-assigns", "LNT-009"},
        {"unconnected-input-port", "LNT-010"}, {"unconnected-output-port", "LNT-010"},
        {"unconnected-inout-port", "LNT-010"}, {"unconnected-unnamed-port", "LNT-010"},
        {"empty-input-connection", "LNT-010"}, {"empty-output-connection", "LNT-010"},
        {"empty-inout-connection", "LNT-010"},
        {"unused-def", "LNT-011"}, {"unused-net", "LNT-011"}, {"unused-implicit-net", "LNT-011"},
        {"unused-variable", "LNT-011"}, {"unused-but-set-net", "LNT-011"},
        {"unused-but-set-variable", "LNT-011"}, {"unused-but-set-port", "LNT-011"},
        {"unused-port", "LNT-011"}, {"unused-argument", "LNT-011"}, {"unused-parameter", "LNT-011"},
        {"unused-type-parameter", "LNT-011"}, {"unused-typedef", "LNT-011"}, {"unused-genvar", "LNT-011"},
        {"inferred-latch", "LNT-013"},
        {"case-wildcard-2state", "LNT-016"}, {"case-not-wildcard", "LNT-016"}, {"casez-with-x", "LNT-016"},
        {"case-incomplete", "LNT-016"}, {"case-none", "LNT-016"}, {"case-default", "LNT-016"},
        {"case-enum", "LNT-016"}, {"case-enum-explicit", "LNT-016"}, {"case-too-complex", "LNT-016"},
        {"case-dup", "LNT-032"}, {"case-overlap", "LNT-032"}, {"case-unreachable", "LNT-032"},
        {"divide-by-zero", "LNT-037"},
        {"shift-count-overflow", "LNT-026"}, {"shift-count-negative", "LNT-026"},
        {"signed-logical-shift", "LNT-035"}, {"unsigned-arith-shift", "LNT-035"},
        {"unsized-concat", "LNT-056"},
        {"unused-import", "LNT-059"}, {"unused-wildcard-import", "LNT-059"}, {"shadow-value", "LNT-059"},
        {"shadow-hierarchy", "LNT-059"}, {"shadow-property", "LNT-059"}, {"dup-import", "LNT-059"},
        {"unnamed-generate", "STY-010"},
    };
    return table;
}

std::string ruleForSlangOption(const std::string& n) {
    auto& t = slangRuleTable();
    auto it = t.find(n);
    return it == t.end() ? std::string() : it->second;
}

} // namespace sverity
