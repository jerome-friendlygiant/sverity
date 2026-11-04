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

namespace slang::ast { class ValueSymbol; }

namespace sverity {

/// A port of a declared library cell: direction (in | out | inout) and role (clock | reset | enable | data_in | data_out |
/// address | write_enable | gated_clock). Roles are what analyses need; nothing technology specific is encoded.
/// "clock" names the clock port this port is synchronous to (ram / blackbox); a data port without one is unresolved for crossing checks.
struct LibPort {
    std::string name, dir, role, clock;
};

/// A library cell the user declares (--library JSON): an elaborated module (sim model or stub) whose body the analysis does not read;
/// it is modelled by its declared kind instead. Technology neutral: the format names modules and ports only (ASIC and FPGA share it).
///   synchronizer  `stages` flops in a chain on `clock` (input -> output)           {"kind":"synchronizer","clock","input","output","stages"}
///   flop          one register: clock, input, output, optional asynchronous reset  {"kind":"flop",...,"reset","reset_active":"low|high","reset_value":0|1}
///   clock_gate    output = clock gated by `enable`; "enable_latched": true when the enable is captured glitch-free inside the cell
///   ram           storage written on `write_clock`, read on `read_clock` (or asynchronously when "sync_read": false); roles in "ports"
///   blackbox      any other cell: port directions and roles in "ports" (unknown cells stay unresolved black boxes)
/// Example: {"cells":[{"name":"SYNC2","kind":"synchronizer","clock":"CLK","input":"D","output":"Q","stages":2}]}
struct LibCell {
    std::string name, kind = "synchronizer", clock, input, output;
    int stages = 2;
    int edge = 1;   // 1 posedge, 2 negedge
    std::string enable, reset;            // clock_gate enable port; flop asynchronous reset port
    bool resetActiveLow = true;
    char resetValue = '0';                // value the flop takes while reset is asserted
    bool enableLatched = false;           // clock_gate: enable is captured by a latch inside the cell
    std::string writeClock, readClock;    // ram
    bool syncRead = true;                 // ram: read data is registered on readClock
    std::vector<LibPort> ports;           // ram / blackbox: declared ports
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
    struct CaseResult { bool applied = false; std::string reason; std::vector<std::string> names; };
    std::vector<CaseResult> caseResults;
    std::map<const slang::ast::ValueSymbol*, int> caseConstants;
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
