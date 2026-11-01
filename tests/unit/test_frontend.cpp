// The slang option -> rule table must only contain real slang warning options (a typo silently demotes a finding to
// an unmapped SLANG-* ID), and every target must be a catalogued rule.
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/text/SourceManager.h"
#include "sverity/catalog.h"
#include "sverity/frontend.h"
#include "testing.h"
using namespace sverity;

TEST(slang_rule_table_uses_real_slang_options_and_catalogued_rules) {
    slang::SourceManager sm;
    slang::DiagnosticEngine engine(sm);
    for (auto& [option, rule] : slangRuleTable()) {
        if (engine.findFromOptionName(option).empty()) {
            std::printf("  unknown slang option in table: %s\n", option.c_str());
            g_failures++;
        }
        CHECK(Catalog::embedded().find(rule) != nullptr);
    }
}
