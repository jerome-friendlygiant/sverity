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
    std::vector<std::string> sdcFiles;      // SDC-subset constraint files (see sverity/sdc.h)
    std::string today;      // ISO date used for waiver expiry; empty = system date
    std::string pathRoot;   // diagnostics paths under this root are reported relative to it
};

/// Never throws for design problems: failures are FATAL diagnostics + execution_status ERROR.
Report analyze(const AnalyzerOptions& opts);

std::string slangVersionString();
std::string currentDateIso();

} // namespace sverity
