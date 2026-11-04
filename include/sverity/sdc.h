// Constraint (SDC subset) model. CST-009: only the commands listed here are interpreted; every other
// command is recorded in `unsupported` so it can be disclosed in the coverage report.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "sverity/diagnostic.h"

namespace sverity::sdc {

/// An object reference as written, e.g. "[get_ports clk]" -> {"get_ports", "clk"}; a bare word "clk_a"
/// (as used in `-group {clk_a clk_b}`) -> {"", "clk_a"}.
struct ObjectRef {
    std::string kind;      // get_ports | get_pins | get_nets | get_clocks | get_cells | all_clocks | all_inputs | "" (bare)
    std::string pattern;   // pattern text exactly as written (no glob expansion)
    bool operator==(const ObjectRef&) const = default;
};

struct ClockDef {
    std::string name;                 // -name (empty if omitted: then derived from first source pattern)
    double periodNs = 0;              // -period; 0 when absent/unparseable
    std::vector<ObjectRef> sources;   // positional object list (ports/pins)
    bool generated = false;           // create_generated_clock
    std::string masterClock;          // -source/-master_clock target, if given
    std::string divideBy, multiplyBy; // raw option text for generated clocks
    SourceLocation loc;
};

struct ClockGroups {
    enum class Kind { Asynchronous, LogicallyExclusive, PhysicallyExclusive, Unspecified };
    Kind kind = Kind::Unspecified;
    std::vector<std::vector<ObjectRef>> groups;   // one entry per -group
    SourceLocation loc;
};

struct PathException {
    enum class Kind { FalsePath, Multicycle, MaxDelay, MinDelay };
    Kind kind = Kind::FalsePath;
    std::vector<ObjectRef> from, to, through;
    std::string valueText;   // multicycle path multiplier / delay value as written
    SourceLocation loc;
};

/// set_input_delay: the port is timed relative to `clock`, i.e. it is synchronous to that clock (not a proof of safety).
struct InputDelay {
    std::string clock;                // -clock target as written (empty when missing)
    std::vector<ObjectRef> ports;
    std::string valueText;
    SourceLocation loc;
};

// Output delays supply external capture clocks to CST-012; they do not prove timing.
struct OutputDelay {
    std::string clock;
    std::vector<ObjectRef> ports;
    std::string valueText;
    SourceLocation loc;
    std::string text;
};
// Binary mode request; target validation precedes model construction.
struct CaseAnalysis {
    int value = 0;
    std::vector<ObjectRef> objects;
    SourceLocation loc;
    std::string text;
};

struct Unsupported {
    std::string command;   // e.g. "set_input_delay"
    std::string text;      // full command text (continuations joined)
    SourceLocation loc;
};

struct ParseProblem {
    std::string message;
    SourceLocation loc;
};

struct Constraints {
    std::vector<std::string> sourcedFiles;
    std::vector<ClockDef> clocks;
    std::vector<ClockGroups> clockGroups;
    std::vector<PathException> exceptions;
    std::vector<InputDelay> inputDelays;
    std::vector<OutputDelay> outputDelays;
    std::vector<CaseAnalysis> caseAnalyses;
    std::vector<Unsupported> unsupported;
    std::vector<ParseProblem> problems;
    void merge(Constraints&& other);
};

/// Parses an SDC (Tcl subset) text. Never throws; malformed commands become `problems`, unknown
/// commands become `unsupported`. Interpreted commands: create_clock, create_generated_clock,
/// set_clock_groups, set_false_path, set_multicycle_path, set_max_delay, set_min_delay, set_input_delay.
/// Output delay supplies CST-012 boundary contracts; binary case analysis is target-validated by the analyser.
/// `fileName` is stored in every SourceLocation.
Constraints parse(std::string_view text, const std::string& fileName);

} // namespace sverity::sdc
