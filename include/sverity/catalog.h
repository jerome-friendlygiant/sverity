// Machine-readable rule registry (docs/rules/catalog.json), embedded at build time.
#pragma once
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sverity {

/// An undecidable-by-lint question a rule may report as Unresolved without blocking by default.
struct ObligationInfo {
    std::string id;
    std::string reason;
};

struct RuleInfo {
    std::string id;
    std::string category;                 // LNT STY SVA CDC RDC CST (the ID prefix)
    std::string title;
    std::string notes;                    // implementation hint from the catalog
    std::string status;                   // proposed | specified | implemented | validated | blocked
    std::string wave;                     // foundation | breadth | advanced (ordering only)
    std::string gap;                      // what a partial rule does not cover (empty if none recorded)
    std::string deck;                     // earliest deck holding the rule: core | domain | 1.0 (decks are cumulative)
    bool cdcDeck = false;                 // member of the CDC 1.0 workflow deck (--deck cdc)
    bool rdcDeck = false;                 // member of the RDC 1.0 workflow deck (--deck rdc)
    std::vector<std::string> classes;     // lint workflow defect classes it covers (docs/rules/LINT_WORKFLOW.md)
    std::vector<ObligationInfo> obligations;   // predicates the rule may emit as obligations; empty = none declared
};

/// True for a known deck name. Decks are cumulative: core is inside domain, domain is inside 1.0.
/// Workflow decks `lint`, `cdc` and `rdc` stand apart: the rules covering a named workflow's frozen scope.
bool isDeck(std::string_view name);
/// True when `rule` belongs to `deck`.
bool inDeck(const RuleInfo& rule, std::string_view deck);
/// True when `id` belongs to `goals` (empty means all families).
bool inGoal(std::string_view id, const std::set<std::string>& goals);

/// Offline rule contract and advice, shared by --explain and SARIF.
std::string ruleHelp(const std::string& id);

class Catalog {
public:
    /// Catalog embedded in the binary. Aborts the process on corrupt embedded data
    /// (the build verifies it in the catalog_sync test).
    static const Catalog& embedded();
    /// Parse an external catalog (for tooling/tests). Returns false and sets error on failure.
    static bool parse(std::string_view json, Catalog& out, std::string& error);

    const std::vector<RuleInfo>& rules() const { return rules_; }
    const RuleInfo* find(std::string_view id) const;
    const std::string& version() const { return version_; }

private:
    std::vector<RuleInfo> rules_;
    std::map<std::string, size_t, std::less<>> index_;
    std::string version_;
};

} // namespace sverity
