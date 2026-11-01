// Rule engine API. A rule is a C++ class or a shared analysis pass over the one elaborated design;
// no rule re-parses RTL (RFC section 2).
#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "sverity/catalog.h"
#include "sverity/frontend.h"
#include "sverity/report.h"
#include "sverity/sdc.h"

namespace sverity {

struct Config {
    std::set<std::string> disabledRules;   // disabled rules stay listed in the coverage report
    std::string profile = "generic";       // generic | asic | fpga
    sdc::Constraints constraints;          // merged from all --sdc files
    std::vector<std::string> sdcFiles;     // names of the loaded constraint files (provenance, CST-021)
    bool constraintsProvided = false;
    /// Per-rule options, key = "<RULE-ID>.<option>" (e.g. "STY-011.max_line_length" = "120").
    std::map<std::string, std::string> ruleOptions;
    std::string option(const std::string& key, const std::string& def) const {
        auto it = ruleOptions.find(key);
        return it == ruleOptions.end() ? def : it->second;
    }
};

struct RuleContext {
    Design& design;
    Report& report;
    const Config& config;
    uint64_t objectsEvaluated = 0;         // rules bump this for coverage accounting
    std::string degradedReason;            // set when the rule could only run in a degraded mode (-> PARTIAL)
};

class Rule {
public:
    virtual ~Rule() = default;
    virtual std::string_view id() const = 0;
    /// Executed = implements the entire catalog title; Partial = documented subset only.
    virtual RuleState completeness() const { return RuleState::Executed; }
    virtual std::string partialReason() const { return {}; }
    /// True when the findings come from slang itself and cannot be switched off per rule.
    virtual bool upstreamBacked() const { return false; }
    virtual void run(RuleContext& ctx) = 0;
};

/// All rules implemented in this build (each id appears at most once).
std::vector<std::unique_ptr<Rule>> makeAllRules();

/// Runs every enabled implemented rule; records a coverage entry for *every* catalog rule
/// (including unimplemented/disabled) and the CST-019 coverage-gap warning.
/// `design` may be null when loading failed (rules are then recorded as NOT_RUN).
void runRules(Design* design, Report& report, const Config& config);

} // namespace sverity
