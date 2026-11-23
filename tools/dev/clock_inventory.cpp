#include <iostream>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "domains/model.h"
#include "domains/relations.h"
#include "sverity/analyzer.h"
#include "sverity/frontend.h"
#include "sverity/sdc.h"

namespace {
std::string quote(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: if (c < 0x20) out += "?"; else out += static_cast<char>(c);
        }
    }
    return out + '"';
}

const char* rootKind(sverity::model::RootKind kind) {
    using sverity::model::RootKind;
    switch (kind) {
        case RootKind::TopPort: return "top_port";
        case RootKind::Logic: return "logic";
        case RootKind::BlackBox: return "black_box";
        case RootKind::Undriven: return "undriven";
        case RootKind::Cycle: return "cycle";
    }
    return "unknown";
}
}

int main(int argc, char** argv) {
    std::string top, filelist, sdcPath;
    std::vector<std::string> libraries;
    std::vector<std::string> slangArgs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--top" || arg == "-f" || arg == "--sdc" || arg == "--library") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (arg == "--top") top = value;
            else if (arg == "-f") filelist = value;
            else if (arg == "--library") libraries.push_back(value);
            else sdcPath = value;
        } else {
            std::cerr << "usage: sverity-clock-inventory --top TOP -f FILELIST --sdc FILE [--library FILE]...\n";
            return 64;
        }
    }
    if (top.empty() || filelist.empty() || sdcPath.empty()) {
        std::cerr << "usage: sverity-clock-inventory --top TOP -f FILELIST --sdc FILE [--library FILE]...\n";
        return 64;
    }

    std::ifstream sdcFile(sdcPath);
    if (!sdcFile) { std::cerr << "cannot read SDC: " << sdcPath << '\n'; return 1; }
    const std::string sdcText{std::istreambuf_iterator<char>(sdcFile), {}};
    sverity::Config config;
    config.constraints = sverity::sdc::parse(sdcText, sdcPath);

    sverity::FrontendOptions options;
    options.slangArgs = {"--single-unit", "--top", top, "-f", filelist};
    sverity::Report report;
    auto design = sverity::loadDesign(options, report);
    if (!design || design->numUpstreamErrors || report.executionStatus() == sverity::ExecutionStatus::Error) {
        std::cerr << "model elaboration failed\n";
        for (const auto& diagnostic : report.diagnostics())
            if (diagnostic.severity == sverity::Severity::Error || diagnostic.severity == sverity::Severity::Fatal)
                std::cerr << diagnostic.message << '\n';
        return 2;
    }
    sverity::loadLibraryFiles(libraries, *design, report);
    for (const auto& diagnostic : report.diagnostics())
        if (diagnostic.ruleId == "CST-018" && diagnostic.severity == sverity::Severity::Error) {
            std::cerr << diagnostic.message << '\n';
            return 2;
        }
    const auto& model = sverity::model::getModel(*design);
    const sverity::model::ClockRelations relations(model, config);

    std::cout << "{\"schema\":1,\"top\":" << quote(top) << ",\"roots\":[";
    for (size_t i = 0; i < model.domains.size(); ++i) {
        if (i) std::cout << ',';
        const auto& domain = model.domains[i];
        std::cout << "{\"domain\":" << i << ",\"root\":" << quote(domain.name)
                  << ",\"root_kind\":" << quote(rootKind(domain.kind))
                  << ",\"sdc_clock\":" << quote(relations.clockName(static_cast<uint32_t>(i)))
                  << ",\"ambiguous\":" << (relations.hasMultipleClocks(static_cast<uint32_t>(i)) ? "true" : "false") << '}';
    }
    std::cout << "],\"declarations\":[";
    std::set<std::string> laterNames;
    std::vector<bool> active(config.constraints.clocks.size(), false);
    for (size_t i = config.constraints.clocks.size(); i-- > 0;) {
        const auto& clock = config.constraints.clocks[i];
        active[i] = laterNames.insert(clock.name).second;
    }
    for (size_t i = 0; i < config.constraints.clocks.size(); ++i) {
        if (i) std::cout << ',';
        const auto& clock = config.constraints.clocks[i];
        const std::string name = clock.name.empty() && !clock.sources.empty() ? clock.sources.front().pattern : clock.name;
        std::vector<size_t> mapped;
        for (size_t d = 0; d < model.domains.size(); ++d) {
            if (!active[i]) break;
            for (const auto& source : clock.sources) {
                const auto root = model.domains[d].root;
                if (root != sverity::model::kNoNode &&
                    relations.clockSourceMatches(source, root, sverity::model::stripBit(model.nameOf(root)))) {
                    mapped.push_back(d);
                    break;
                }
            }
        }
        std::cout << "{\"name\":" << quote(name) << ",\"generated\":" << (clock.generated ? "true" : "false")
                  << ",\"active\":" << (active[i] ? "true" : "false") << ",\"mapped_domains\":[";
        for (size_t j = 0; j < mapped.size(); ++j) { if (j) std::cout << ','; std::cout << mapped[j]; }
        std::cout << "],\"unmapped\":" << (active[i] && mapped.empty() ? "true" : "false") << '}';
    }
    std::cout << "],\"parse_problems\":[";
    for (size_t i = 0; i < config.constraints.problems.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << quote(config.constraints.problems[i].message);
    }
    std::cout << "],\"unsupported_commands\":[";
    for (size_t i = 0; i < config.constraints.unsupported.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << quote(config.constraints.unsupported[i].command);
    }
    std::cout << "]}\n";
}
