#include <algorithm>
#include <string>
#include "sverity/analyzer.h"
#include "testing.h"
using namespace sverity;

TEST(crossing_mixed_edges_unknown_relationship_and_launch_file) {
    const std::string root = SVERITY_SOURCE_DIR;
    for (bool constrained : {false, true}) {
        AnalyzerOptions opts;
        opts.frontend.slangArgs = {"--top", "top", "-I" + root + "/tests/data/crossing", root + "/tests/data/crossing/design.sv"};
        if (constrained) opts.sdcFiles = {root + "/tests/fixtures/_common/async.sdc"};
        auto report = analyze(opts);
        bool found = false;
        for (const auto& d : report.diagnostics()) {
            if (d.ruleId != "CDC-003" || !d.crossing) continue;
            found = true;
            const auto& c = *d.crossing;
            CHECK_EQ(c.launch.signal, "top.src");
            CHECK_EQ(c.capture.signal, "top.result");
            CHECK_EQ(c.launch.clockDomain, "top.clka");
            CHECK_EQ(c.capture.clockDomain, "top.clkb");
            CHECK_EQ(c.launch.edge, "negedge");
            CHECK_EQ(c.capture.edge, "posedge");
            CHECK_EQ(c.relationship, constrained ? "ASYNC" : "UNKNOWN");
            CHECK_EQ(c.launch.sdcClock.empty(), !constrained);
            CHECK_EQ(c.capture.sdcClock.empty(), !constrained);
            CHECK_EQ(c.widthBits, 1u);
            CHECK(!c.logicOnPath);
            CHECK(c.launch.location.file.ends_with("launch.svh"));
            CHECK_EQ(c.launch.location.line, 1u);
            CHECK(c.capture.location.file.ends_with("design.sv"));
            CHECK_EQ(c.capture.location.line, 4u);
            auto launch = std::find_if(d.evidence.begin(), d.evidence.end(), [](const auto& e) { return e.kind == "launch_register"; });
            CHECK(launch != d.evidence.end());
            if (launch != d.evidence.end()) CHECK_EQ(launch->location, c.launch.location);
        }
        CHECK(found);
        const auto json = report.toJson().dump();
        CHECK(json.find("\"crossing\"") != std::string::npos);
        CHECK(json.find("launch.svh") != std::string::npos);
        CHECK(report.toSarif().dump().find("\"crossing\"") != std::string::npos);
        CHECK(report.toText(false, 0, "", false, true).find("[negedge]") != std::string::npos);
    }
}

TEST(crossing_unavailable_edge_and_location_stay_unknown) {
    Diagnostic d;
    d.ruleId = "CDC-003";
    d.crossing = Crossing{};
    Report report;
    report.add(d);
    report.finalize();
    const auto json = report.toJson();
    const auto& crossing = json["diagnostics"].asArray()[0]["crossing"];
    CHECK_EQ(crossing["launch"]["edge"].asString(), "unknown");
    CHECK(crossing["launch"]["location"].asObject().empty());
    CHECK_EQ(crossing["relationship"].asString(), "UNKNOWN");
}
