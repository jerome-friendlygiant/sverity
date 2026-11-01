#include <map>
#include <string>

#include "sverity/frontend.h"

#include "slang/diagnostics/AllDiags.h"
#include "slang/diagnostics/Diagnostics.h"

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
        {"case-enum", "LNT-017"}, {"case-enum-explicit", "LNT-017"}, {"case-too-complex", "LNT-016"},
        {"case-dup", "LNT-032"}, {"case-overlap", "LNT-032"}, {"case-unreachable", "LNT-032"},
        {"divide-by-zero", "LNT-037"},
        {"shift-count-overflow", "LNT-026"}, {"shift-count-negative", "LNT-026"},
        {"signed-logical-shift", "LNT-035"}, {"unsigned-arith-shift", "LNT-035"},
        {"unsized-concat", "LNT-056"},
        {"unused-import", "LNT-059"}, {"unused-wildcard-import", "LNT-059"}, {"shadow-value", "LNT-059"},
        {"shadow-hierarchy", "LNT-059"}, {"shadow-property", "LNT-059"}, {"dup-import", "LNT-059"},
        {"unnamed-generate", "STY-010"},
        {"redefinition", "LNT-002"}, {"redefinition-different-type", "LNT-002"}, {"duplicate-definition", "LNT-002"},
        {"unknown-library", "LNT-002"},
        {"implicit-port-type-mismatch", "LNT-003"}, {"port-coercion", "LNT-003"}, {"net-inconsistent", "LNT-003"},
        {"bad-procedural-force", "LNT-031"},
        {"multi-write", "LNT-060"}, {"read-write", "LNT-060"},
        {"unknown-sys-name", "LNT-061"},
        {"unused-package-var", "LNT-059"}, {"unused-package-typedef", "LNT-059"}, {"unused-package-parameter", "LNT-059"},
        {"unused-package-type-parameter", "LNT-059"}, {"unused-package-assertion-decl", "LNT-059"}, {"unused-package-subroutine", "LNT-059"},
    };
    return table;
}

std::string ruleForSlangOption(const std::string& n) {
    auto& t = slangRuleTable();
    auto it = t.find(n);
    return it == t.end() ? std::string() : it->second;
}

} // namespace sverity

namespace sverity {

// slang errors have no option name; they are mapped by diagnostic code. Using the diag:: constants makes a misspelt
// name a compile error. Anything not listed stays LNT-001 (syntax, preprocess and other elaboration failures).
std::string ruleForSlangDiag(slang::DiagCode code) {
    using namespace slang;
    static const std::map<DiagCode, const char*> table = {
        // LNT-002: missing / duplicate / unresolved module, package and symbol references
        {diag::UnknownPackage, "LNT-002"}, {diag::UnknownModule, "LNT-002"}, {diag::UnknownInterface, "LNT-002"},
        {diag::UnknownPrimitive, "LNT-002"}, {diag::UndeclaredIdentifier, "LNT-002"}, {diag::TypoIdentifier, "LNT-002"},
        {diag::UnknownClassOrPackage, "LNT-002"}, {diag::UsedBeforeDeclared, "LNT-002"}, {diag::UnknownMember, "LNT-002"},
        {diag::UnknownPackageMember, "LNT-002"}, {diag::UnknownClassMember, "LNT-002"},
        {diag::HierarchicalRefUnknownModule, "LNT-002"}, {diag::UndeclaredButFoundPackage, "LNT-002"},
        {diag::AmbiguousWildcardImport, "LNT-002"}, {diag::ImportNameCollision, "LNT-002"}, {diag::NotAType, "LNT-002"},
        {diag::NotAValue, "LNT-002"}, {diag::NotAHierarchicalScope, "LNT-002"}, {diag::MultipleTopDupName, "LNT-002"},
        {diag::CouldNotResolveHierarchicalPath, "LNT-002"}, {diag::PackageExportNotImported, "LNT-002"},
        {diag::DefinitionUsedAsType, "LNT-002"}, {diag::DefinitionUsedAsValue, "LNT-002"},
        // LNT-003: illegal port connections
        {diag::MixingOrderedAndNamedPorts, "LNT-003"}, {diag::DuplicateWildcardPortConnection, "LNT-003"},
        {diag::DuplicatePortConnection, "LNT-003"}, {diag::TooManyPortConnections, "LNT-003"},
        {diag::PortDoesNotExist, "LNT-003"}, {diag::PortConnDimensionsMismatch, "LNT-003"},
        {diag::InterfacePortNotConnected, "LNT-003"}, {diag::InterfacePortTypeMismatch, "LNT-003"},
        {diag::ImplicitNamedPortNotFound, "LNT-003"}, {diag::PortConnArrayMismatch, "LNT-003"},
        {diag::InOutVarPortConn, "LNT-003"}, {diag::InOutUWireConn, "LNT-003"}, {diag::ModportConnMismatch, "LNT-003"},
        {diag::RefPortUnconnected, "LNT-003"}, {diag::RefPortUnnamedUnconnected, "LNT-003"},
        {diag::ImplicitConnNetInconsistent, "LNT-003"}, {diag::MismatchedUserDefPortConn, "LNT-003"},
        {diag::MismatchedUserDefPortDir, "LNT-003"}, {diag::InterfacePortInvalidExpression, "LNT-003"},
        {diag::InvalidHierarchicalIfacePortConn, "LNT-003"},
        // LNT-014: always_ff / always_comb / always procedure misuse
        {diag::BlockingInAlwaysFF, "LNT-014"}, {diag::AlwaysFFEventControl, "LNT-014"},
        {diag::ForkJoinAlwaysComb, "LNT-014"}, {diag::AlwaysWithoutTimingControl, "LNT-014"},
        // LNT-009: driver conflicts that slang reports as errors
        {diag::MultipleUWireDrivers, "LNT-009"}, {diag::MultipleUDNTDrivers, "LNT-009"},
        // LNT-031: illegal assignment to a net / variable kind
        {diag::AssignToNet, "LNT-031"}, {diag::BadProceduralAssign, "LNT-031"}, {diag::BadForceNetType, "LNT-031"},
        {diag::ExpressionNotAssignable, "LNT-031"}, {diag::AssignmentToConstVar, "LNT-031"},
        {diag::CantModifyConst, "LNT-031"}, {diag::SelectOfVectoredNet, "LNT-031"},
        // LNT-050: port direction / modport misuse
        {diag::InputPortAssign, "LNT-050"}, {diag::InOutPortCannotBeVariable, "LNT-050"}, {diag::InOutUWirePort, "LNT-050"},
        {diag::DirectionOnInterfacePort, "LNT-050"}, {diag::DirectionWithInterfacePort, "LNT-050"},
        {diag::InvalidModportAccess, "LNT-050"}, {diag::NotAllowedInModport, "LNT-050"},
        {diag::RefPortMustBeVariable, "LNT-050"}, {diag::WriteToInputClockVar, "LNT-050"},
        // LNT-016: duplicate default etc. in case statements
        {diag::MultipleDefaultCases, "LNT-016"}, {diag::CaseStatementEmpty, "LNT-016"},
        // LNT-018 / LNT-038: constant evaluation facts slang proves
        {diag::StaticAssert, "LNT-018"},
        // LNT-006
        {diag::IndexValueInvalid, "LNT-006"}, {diag::RangeWidthOverflow, "LNT-006"}, {diag::ScopeIndexOutOfRange, "LNT-006"},
    };
    auto it = table.find(code);
    return it == table.end() ? std::string() : std::string(it->second);
}

} // namespace sverity
