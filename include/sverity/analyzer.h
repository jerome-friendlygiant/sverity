// One-call embeddable API: load -> analyse -> waive -> finalize.
#pragma once
#include <string>
#include <vector>

#include "sverity/rule.h"

namespace sverity {

struct AnalyzerOptions {
    FrontendOptions frontend;
    Config config;
    std::vector<std::string> waiverFiles;
    std::vector<std::string> libraryFiles;  // library cell descriptions (see LibCell in sverity/frontend.h)
    std::vector<std::string> sdcFiles;      // SDC-subset constraint files (see sverity/sdc.h)
    bool sourceWaivers = true;
    std::string today;      // ISO date used for waiver expiry; empty = system date
    std::string pathRoot;   // diagnostics paths under this root are reported relative to it
    std::string timingsFile; // write phase time and peak memory here; empty = do not measure
};

/// Never throws for design problems: failures are FATAL diagnostics + execution_status ERROR.
Report analyze(const AnalyzerOptions& opts);

/// Reads `--library` cell descriptions into the design (before its model is built). A file or entry that cannot be used is a
/// CST-018 ERROR in `report`, and its cells stay ordinary modules.
void loadLibraryFiles(const std::vector<std::string>& files, Design& design, Report& report);

std::string slangVersionString();
std::string currentDateIso();

} // namespace sverity
