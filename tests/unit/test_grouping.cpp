#include <algorithm>
#include <string>
#include "sverity/report.h"
#include "sverity/waiver.h"
#include "testing.h"
using namespace sverity;

namespace {
Diagnostic finding(const std::string& rule, Severity severity, uint32_t line, const std::string& message) {
    Diagnostic d; d.ruleId=rule; d.severity=severity; d.location={"/work/project/rtl/a.sv",line,1};
    d.hierarchy="top"; d.message=message; d.signals={"target"}; d.groupingUnit="top"; d.groupingProcedureOrdinal=1;
    d.evidence.push_back({"procedure", "clocked", {}}); return d;
}
Report sample() {
    Report r; r.setPathRoot("/work/project");
    r.add(finding("LNT-025",Severity::Warning,2,"first"));
    auto e=finding("LNT-025",Severity::Error,3,"second"); e.count=2; r.add(e);
    r.add(finding("LNT-053",Severity::Warning,4,"other rule"));
    r.finalize(); return r;
}
}
TEST(root_group_json_sarif_keep_each_diagnostic) {
    auto r=sample(); auto j=r.toJson(); auto s=r.toSarif();
    CHECK_EQ(j["diagnostics"].asArray().size(),3u);
    CHECK_EQ(s["runs"].asArray()[0]["results"].asArray().size(),3u);
    for(size_t i=0;i<3;i++) {
        const auto& d=j["diagnostics"].asArray()[i]; const auto& result=s["runs"].asArray()[0]["results"].asArray()[i];
        CHECK(!d["fingerprint"].asString().empty()); CHECK(!d["root_cause"].asString().empty());
        CHECK_EQ(d["root_cause"].asString(),result["properties"]["root_cause"].asString());
        CHECK_EQ(d["fingerprint"].asString(),result["partialFingerprints"]["sverity/v1"].asString());
        CHECK(!d["evidence"].asArray().empty());
    }
}
TEST(root_group_identity_survives_checkout_and_report_root_changes) {
    auto make=[](const char* checkout,const char* root) {
        Report r; r.setPathRoot(root);
        auto a=finding("LNT-025",Severity::Warning,2,"first"); a.location.file=std::string(checkout)+"/rtl/a.sv"; r.add(a);
        auto b=finding("LNT-011",Severity::Warning,8,"unused"); b.location.file=std::string(checkout)+"/tb/a.sv"; r.add(b);
        r.finalize(); return r;
    };
    auto a=make("/one/checkout","/one/checkout"),b=make("/two/checkout","/two/checkout/rtl");
    for(const char* rule:{"LNT-025","LNT-011"}) {
        std::string left,right;
        for(const auto& d:a.diagnostics()) if(d.ruleId==rule) left=d.rootCause;
        for(const auto& d:b.diagnostics()) if(d.ruleId==rule) right=d.rootCause;
        CHECK(!left.empty()); CHECK_EQ(left,right);
        for (const auto& x:a.diagnostics()) for (const auto& y:b.diagnostics())
            if (x.ruleId==rule && y.ruleId==rule) CHECK_EQ(x.fingerprint,y.fingerprint);
    }
}
TEST(root_group_console_exposes_worst_and_severity_mix) {
    auto r=sample(); auto t=r.toText(false,0,"complete.json",true);
    CHECK(t.find("ERROR [LNT-025] 3 occurrences")!=std::string::npos);
    CHECK(t.find("WARNING=1, ERROR=2")!=std::string::npos);
    CHECK(t.find("Console grouped:")!=std::string::npos);
    CHECK(t.find("complete.json")!=std::string::npos);
}
TEST(root_group_never_merges_different_rules_or_unsupported_findings) {
    auto r=sample(); CHECK_EQ(r.diagnostics()[0].rootCause,r.diagnostics()[1].rootCause);
    CHECK(r.diagnostics()[0].rootCause!=r.diagnostics()[2].rootCause);
    Report x; x.add(finding("LNT-011",Severity::Warning,2,"same")); x.add(finding("LNT-011",Severity::Warning,3,"same")); x.finalize();
    CHECK(x.diagnostics()[0].rootCause!=x.diagnostics()[1].rootCause);
}
TEST(root_group_console_without_complete_report_stays_ungrouped) {
    auto r=sample(); CHECK_EQ(r.toText(false,0,"",true),r.toText(false,0,"",false));
    CHECK(r.toText(false,0,"",true).find("Console grouped:")==std::string::npos);
}
TEST(root_group_counts_preserve_occurrences) {
    auto r=sample(); auto t=r.toText(false,0,"report.json",true);
    CHECK(t.find("Console grouped: 4 occurrences in 2 root-cause groups")!=std::string::npos);
    CHECK(t.find("3 occurrences")!=std::string::npos); CHECK(t.find("1 occurrence:")!=std::string::npos);
}
TEST(root_group_waiver_matches_one_fingerprint) {
    auto r=sample(); auto fingerprint=r.diagnostics()[0].fingerprint;
    auto oldGroup=r.diagnostics()[0].rootCause; WaiverSet set; std::string error;
    CHECK(WaiverSet::parse("{\"waivers\":[{\"id\":\"W\",\"rule\":\"LNT-025\",\"fingerprint\":\""+fingerprint+"\",\"reason\":\"checked\",\"owner\":\"team\"}]}",set,error));
    applyWaivers(r,set,"2026-01-01"); r.finalize();
    unsigned waived=0,unwaived=0;
    for(const auto& d:r.diagnostics()) if(d.ruleId=="LNT-025") {
        CHECK_EQ(d.rootCause,oldGroup);
        if(d.disposition==Disposition::Waived) { waived++; CHECK_EQ(d.fingerprint,fingerprint); } else unwaived++;
    }
    CHECK_EQ(waived,1u); CHECK_EQ(unwaived,1u);
    CHECK(r.toText(false,0,"report.json",true).find("WAIVED=1")!=std::string::npos);
}

TEST(root_group_identity_ignores_input_order_for_coalesced_instances) {
    auto build=[](bool reverse) {
        Report r;
        auto a=finding("LNT-011",Severity::Warning,4,"same"); a.hierarchy="top.a";
        auto b=a; b.hierarchy="top.b";
        if(reverse) { r.add(b); r.add(a); } else { r.add(a); r.add(b); }
        r.finalize(); return r;
    };
    auto a=build(false), b=build(true);
    CHECK_EQ(a.diagnostics().size(),1u); CHECK_EQ(b.diagnostics().size(),1u);
    CHECK_EQ(a.diagnostics()[0].rootCause,b.diagnostics()[0].rootCause);
}
TEST(root_group_identity_normalizes_paths_embedded_in_messages) {
    auto build=[](const char* root) {
        Report r; r.setPathRoot(root);
        Diagnostic d; d.ruleId="LNT-001"; d.severity=Severity::Error;
        d.message=std::string("cannot read '")+root+"/missing.sv'"; r.add(d); r.finalize(); return r;
    };
    auto a=build("/first/checkout"),b=build("/second/checkout");
    CHECK_EQ(a.diagnostics()[0].rootCause,b.diagnostics()[0].rootCause);
}

TEST(root_group_identity_ignores_unrelated_external_checkout_prefix) {
    auto build=[](const char* checkout) {
        Report r; r.setPathRoot(checkout);
        auto a=finding("LNT-011",Severity::Warning,2,"local"); a.location.file=std::string(checkout)+"/rtl/a.sv"; r.add(a);
        auto b=finding("LNT-024",Severity::Warning,3,"external"); b.location.file="/opt/shared/external.sv"; r.add(b);
        r.finalize(); return r;
    };
    auto a=build("/alpha/work/checkout"), b=build("/beta/work/checkout"); std::string x,y;
    for(const auto& d:a.diagnostics()) if(d.ruleId=="LNT-011") x=d.rootCause;
    for(const auto& d:b.diagnostics()) if(d.ruleId=="LNT-011") y=d.rootCause;
    CHECK(!x.empty()); CHECK_EQ(x,y);
}

TEST(root_group_limit_never_hides_error_or_fatal_groups) {
    Report r;
    for (unsigned i=1; i<=3; i++) r.add(finding("LNT-011",Severity::Error,i,"distinct error"));
    r.add(finding("LNT-053",Severity::Warning,4,"warning")); r.finalize();
    auto text=r.toText(false,1,"report.json",true);
    size_t found=0, pos=0; while ((pos=text.find("ERROR [LNT-011]",pos))!=std::string::npos) { ++found; ++pos; }
    CHECK_EQ(found,3u); CHECK(text.find("output truncated")!=std::string::npos);
}

TEST(root_group_waiver_split_preserves_shared_root_cause) {
    Report r;
    auto a=finding("LNT-025",Severity::Warning,7,"same"); a.hierarchy="top.a";
    auto b=a; b.hierarchy="top.b"; r.add(a); r.add(b); r.finalize();
    CHECK_EQ(r.diagnostics().size(),1u); const auto group=r.diagnostics()[0].rootCause;
    WaiverSet set; std::string error;
    CHECK(WaiverSet::parse(R"({"waivers":[{"id":"W","rule":"LNT-025","hierarchy":"top.a","reason":"checked","owner":"team"}]})",set,error));
    applyWaivers(r,set,"2026-01-01"); r.finalize();
    CHECK_EQ(r.diagnostics().size(),2u);
    for(const auto& d:r.diagnostics()) CHECK_EQ(d.rootCause,group);
    CHECK(r.toText(false,0,"report.json",true).find("WAIVED=1")!=std::string::npos);
}

TEST(root_group_distinguishes_evidence_in_different_files) {
    Report r;
    auto a=finding("LNT-011",Severity::Warning,2,"same");
    a.evidence[0].location={"/work/project/first.sv",5,1};
    auto b=a; b.evidence[0].location.file="/work/project/second.sv";
    r.add(a); r.add(b); r.finalize();
    CHECK_EQ(r.diagnostics().size(),2u); CHECK(r.diagnostics()[0].rootCause!=r.diagnostics()[1].rootCause);
}

TEST(root_group_distinguishes_procedures_at_same_macro_location) {
    Report r;
    auto a=finding("LNT-025",Severity::Warning,2,"first");
    auto b=finding("LNT-025",Severity::Warning,3,"second"); b.groupingProcedureOrdinal=2;
    r.add(a); r.add(b); r.finalize();
    CHECK(r.diagnostics()[0].rootCause!=r.diagnostics()[1].rootCause);
}

TEST(root_group_distinguishes_macro_expanded_design_units) {
    Report r;
    auto a=finding("LNT-025",Severity::Warning,2,"first");
    auto b=finding("LNT-025",Severity::Warning,3,"second"); b.groupingUnit="other_module";
    r.add(a); r.add(b); r.finalize();
    CHECK(r.diagnostics()[0].rootCause!=r.diagnostics()[1].rootCause);
}

TEST(finding_identity_ignores_presentation_wording) {
    auto a=finding("CDC-003",Severity::Error,2,"old headline");
    a.identityKey="direct-crossing";
    auto b=a; b.message="new headline"; b.evidence[0].text="new explanation";
    b.nextStep="new advice"; b.severity=Severity::Warning;
    CHECK_EQ(computeFingerprint(a),computeFingerprint(b));
    Report left,right; left.add(a); right.add(b); left.finalize(); right.finalize();
    CHECK_EQ(left.diagnostics()[0].rootCause,right.diagnostics()[0].rootCause);
}

TEST(finding_identity_distinguishes_signals_on_one_line) {
    auto a=finding("CDC-003",Severity::Error,2,"crossing");
    auto b=a; b.signals={"other_target"};
    CHECK(computeFingerprint(a)!=computeFingerprint(b));
    Report r; r.add(a); r.add(b); r.finalize(); CHECK_EQ(r.diagnostics().size(),2u);
    CHECK(r.diagnostics()[0].fingerprint!=r.diagnostics()[1].fingerprint);
}

TEST(finding_identity_distinguishes_semantic_keys_and_evidence_locations) {
    auto a=finding("CDC-029",Severity::Warning,2,"capture"); auto b=a;
    a.identityKey="input[0]"; b.identityKey="input[1]";
    CHECK(computeFingerprint(a)!=computeFingerprint(b));
    b=a; b.evidence[0].location={"other.sv",8,2};
    CHECK(computeFingerprint(a)!=computeFingerprint(b));
}
