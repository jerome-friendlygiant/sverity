// slang adaptation layer. slang is the single mandatory HDL frontend; everything it reports is
// preserved in the SVerity report (RFC section 3 "Preserve upstream compiler diagnostics").
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sverity/report.h"

namespace slang { class DiagCode; }
namespace slang::driver { class Driver; }
namespace slang::ast { class Compilation; }
namespace slang::analysis { class AnalysisManager; }
namespace sverity::model { class Model; }

namespace sverity {

/// A library cell the user declares (--library JSON): an elaborated module (sim model or stub) that the analysis treats as a
/// synchroniser of `stages` flops on `edge` of its `clock` port instead of looking at its body. The format is technology
/// neutral: it names modules and ports only. {"cells":[{"name":"SYNC2","kind":"synchronizer","clock":"CLK","input":"D","output":"Q","stages":2}]}
struct LibCell {
    std::string name, kind = "synchronizer", clock, input, output;
    int stages = 2;
    int edge = 1;   // 1 posedge, 2 negedge
};

/// One elaborated design: slang driver + compilation + slang's driver/dataflow analysis.
/// The shared model every rule and later graph/proof layer consumes (one parse, one elaboration).
class Design {
public:
    Design();
    ~Design();
    std::unique_ptr<slang::driver::Driver> driver;
    std::unique_ptr<slang::ast::Compilation> compilation;
    std::unique_ptr<slang::analysis::AnalysisManager> analysis;
    /// Lazily built by model::getModel(): the shared connectivity/clock/reset model (one per run).
    std::shared_ptr<model::Model> model;
    std::map<std::string, LibCell> libCells;   // by module (definition) name
    int numUpstreamErrors = 0;
    int numUpstreamWarnings = 0;
};

struct FrontendOptions {
    /// Arguments forwarded to slang's driver (files, -f filelists, -D, -I, --top, -G, -y ...).
    std::vector<std::string> slangArgs;
    /// By default every slang warning is enabled (-Weverything) so nothing is silently ignored by
    /// slang's default warning set. Disabling this is recorded as a coverage gap.
    bool enableAllUpstreamWarnings = true;
};

/// Loads, parses, elaborates and analyses the design. Always returns a Design when parsing could
/// start; returns nullptr only for command-line / file-loading failures (a FATAL diagnostic and an
/// execution error are recorded in the report in that case).
std::unique_ptr<Design> loadDesign(const FrontendOptions& opts, Report& report);

/// Maps a slang warning option name (e.g. "unused-net") to the SVerity rule that covers it.
/// Returns "" when no catalog rule covers it (reported as "SLANG-<name>").
std::string ruleForSlangOption(const std::string& optionName);
/// Maps a slang *error* code (no option name) to its catalog rule; "" = unmapped (stays LNT-001 elaboration failure).
std::string ruleForSlangDiag(slang::DiagCode code);
/// The whole mapping table (slang option name -> rule ID); a unit test checks every key is a real slang option.
const std::map<std::string, std::string>& slangRuleTable();

} // namespace sverity
