#include <string>
#include <filesystem>
#include <fstream>
#include <vector>

#include "sverity/sdc.h"
#include "testing.h"

using namespace sverity;
using R = sdc::ObjectRef;

namespace {

bool hasProblem(const sdc::Constraints& c, const std::string& needle) {
    for (const auto& p : c.problems) {
        if (p.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST(sdc_create_clock_get_ports) {
    auto c = sdc::parse("create_clock -name clk -period 10 [get_ports clk]\n", "top.sdc");
    CHECK(c.problems.empty());
    CHECK(c.unsupported.empty());
    CHECK_EQ(c.clocks.size(), 1u);
    if (c.clocks.size() != 1) return;
    const auto& k = c.clocks[0];
    CHECK_EQ(k.name, std::string("clk"));
    CHECK(k.periodNs == 10.0);
    CHECK(!k.generated);
    std::vector<R> want = {R{"get_ports", "clk"}};
    CHECK_EQ(k.sources, want);
    CHECK_EQ(k.loc.file, std::string("top.sdc"));
}

TEST(sdc_create_clock_name_from_source_and_ignored_options) {
    // No -name: the name comes from the first source. -waveform and -add are accepted and ignored.
    auto c = sdc::parse("create_clock -period 2.5 -waveform {0 1.25} -add [get_ports {clk_a clk_b}]\n", "f");
    CHECK(c.problems.empty());
    CHECK_EQ(c.clocks.size(), 1u);
    if (c.clocks.size() != 1) return;
    CHECK_EQ(c.clocks[0].name, std::string("clk_a"));
    CHECK(c.clocks[0].periodNs == 2.5);
    std::vector<R> want = {R{"get_ports", "clk_a"}, R{"get_ports", "clk_b"}};
    CHECK_EQ(c.clocks[0].sources, want);
}

TEST(sdc_multiline_continuation) {
    const char* text =
        "create_clock -name clk_core \\\n"
        "   -period 4.0 \\\n"
        "   [get_ports core_clk]\n"
        "set_output_delay 1.2 \\\n"
        "  -clock clk_core \\\n"
        "  [get_ports din]\n";
    auto c = sdc::parse(text, "f");
    CHECK(c.problems.empty());
    CHECK_EQ(c.clocks.size(), 1u);
    if (c.clocks.size() == 1) {
        CHECK_EQ(c.clocks[0].name, std::string("clk_core"));
        CHECK(c.clocks[0].periodNs == 4.0);
        CHECK_EQ(c.clocks[0].loc.line, 1u);
    }
    CHECK_EQ(c.outputDelays.size(), 1u);
    if (c.outputDelays.size() == 1) {
        // Continuations are joined with a single space, so the text reads as one line.
        CHECK_EQ(c.outputDelays[0].text, std::string("set_output_delay 1.2 -clock clk_core [get_ports din]"));
        CHECK_EQ(c.outputDelays[0].loc.line, 4u);
    }
}

TEST(sdc_clock_groups_async_two_groups) {
    const char* text =
        "set_clock_groups -asynchronous \\\n"
        "  -group [get_clocks clk_a] \\\n"
        "  -group [get_clocks {clk_b clk_c clk_d}]\n";
    auto c = sdc::parse(text, "f");
    CHECK(c.problems.empty());
    CHECK(c.unsupported.empty());
    CHECK_EQ(c.clockGroups.size(), 1u);
    if (c.clockGroups.size() != 1) return;
    const auto& g = c.clockGroups[0];
    CHECK(g.kind == sdc::ClockGroups::Kind::Asynchronous);
    CHECK_EQ(g.groups.size(), 2u);
    if (g.groups.size() != 2) return;
    std::vector<R> g0 = {R{"get_clocks", "clk_a"}};
    std::vector<R> g1 = {R{"get_clocks", "clk_b"}, R{"get_clocks", "clk_c"}, R{"get_clocks", "clk_d"}};
    CHECK_EQ(g.groups[0], g0);
    CHECK_EQ(g.groups[1], g1);
    CHECK_EQ(g.loc.line, 1u);
}

TEST(sdc_clock_groups_bare_names_and_kinds) {
    auto c = sdc::parse("set_clock_groups -logically_exclusive -group clk_a -group {clk_b clk_c}\n"
                        "set_clock_groups -physically_exclusive -group clk_x -group clk_y\n",
                        "f");
    CHECK(c.problems.empty());
    CHECK_EQ(c.clockGroups.size(), 2u);
    if (c.clockGroups.size() != 2) return;
    CHECK(c.clockGroups[0].kind == sdc::ClockGroups::Kind::LogicallyExclusive);
    CHECK_EQ(c.clockGroups[0].groups.size(), 2u);
    CHECK(c.clockGroups[1].kind == sdc::ClockGroups::Kind::PhysicallyExclusive);
    std::vector<R> want = {R{"", "clk_y"}};
    CHECK_EQ(c.clockGroups[1].groups[1], want);
}

TEST(sdc_clock_groups_missing_kind_is_problem) {
    auto c = sdc::parse("set_clock_groups -group clk_a -group clk_b\n", "f");
    CHECK(c.clockGroups.empty());
    CHECK(hasProblem(c, "missing -asynchronous"));
}

TEST(sdc_false_path_from_to) {
    auto c = sdc::parse("set_false_path -setup -from [get_clocks clk_a] -to [get_clocks clk_b]\n"
                        "set_false_path -through [get_pins u_x/q_reg*/Q] -hold\n",
                        "f");
    CHECK(c.problems.empty());
    CHECK_EQ(c.exceptions.size(), 2u);
    if (c.exceptions.size() != 2) return;
    const auto& e = c.exceptions[0];
    CHECK(e.kind == sdc::PathException::Kind::FalsePath);
    std::vector<R> from = {R{"get_clocks", "clk_a"}};
    std::vector<R> to = {R{"get_clocks", "clk_b"}};
    CHECK_EQ(e.from, from);
    CHECK_EQ(e.to, to);
    CHECK(e.through.empty());
    std::vector<R> thr = {R{"get_pins", "u_x/q_reg*/Q"}};
    CHECK_EQ(c.exceptions[1].through, thr);
    CHECK(c.exceptions[1].from.empty());
}

TEST(sdc_multicycle_and_delays) {
    auto c = sdc::parse("set_multicycle_path 2 -setup -from [get_clocks a] -to [get_clocks b]\n"
                        "set_max_delay 3.5 -from [get_ports x]\n"
                        "set_min_delay -0.5 -to [get_ports y]\n",
                        "f");
    CHECK(c.problems.empty());
    CHECK_EQ(c.exceptions.size(), 3u);
    if (c.exceptions.size() != 3) return;
    CHECK(c.exceptions[0].kind == sdc::PathException::Kind::Multicycle);
    CHECK_EQ(c.exceptions[0].valueText, std::string("2"));
    CHECK(c.exceptions[1].kind == sdc::PathException::Kind::MaxDelay);
    CHECK_EQ(c.exceptions[1].valueText, std::string("3.5"));
    CHECK(c.exceptions[2].kind == sdc::PathException::Kind::MinDelay);
    CHECK_EQ(c.exceptions[2].valueText, std::string("-0.5"));
    CHECK_EQ(c.exceptions[2].to.size(), 1u);
}

TEST(sdc_generated_clock) {
    auto c = sdc::parse("create_generated_clock -name clk_div2 -source [get_ports clk] -divide_by 2 "
                        "[get_pins div_reg/Q]\n"
                        "create_generated_clock -name g3 -source [get_pins a/Q] -master_clock [get_clocks clk] "
                        "-multiply_by 3 [get_pins b/Q]\n",
                        "f");
    CHECK(c.problems.empty());
    CHECK_EQ(c.clocks.size(), 2u);
    if (c.clocks.size() != 2) return;
    const auto& d = c.clocks[0];
    CHECK(d.generated);
    CHECK_EQ(d.name, std::string("clk_div2"));
    CHECK_EQ(d.masterClock, std::string("clk"));
    CHECK_EQ(d.divideBy, std::string("2"));
    CHECK(d.multiplyBy.empty());
    std::vector<R> tgt = {R{"get_pins", "div_reg/Q"}};
    CHECK_EQ(d.sources, tgt);
    const auto& m = c.clocks[1];
    CHECK_EQ(m.masterClock, std::string("clk"));  // -master_clock wins over -source
    CHECK_EQ(m.multiplyBy, std::string("3"));
}

TEST(sdc_generated_clock_missing_source_is_problem) {
    auto c = sdc::parse("create_generated_clock -name g -divide_by 2 [get_pins q/Q]\n", "f");
    CHECK(hasProblem(c, "missing required -source"));
    CHECK(c.clocks.empty());
}

TEST(sdc_comments) {
    const char* text =
        "# header comment with [brackets] and {braces\n"
        "create_clock -name a -period 5 [get_ports a]   ;# trailing comment\n"
        "   # indented comment\n"
        "set x 1 ; # comment after semicolon\n"
        "# comment that continues \\\n"
        "set_output_delay 1 [get_ports p]\n";   // still part of the comment: not a command
    auto c = sdc::parse(text, "f");
    CHECK(c.problems.empty());
    CHECK_EQ(c.clocks.size(), 1u);
    CHECK(c.unsupported.empty());
}

TEST(sdc_unsupported_commands_recorded) {
    auto c = sdc::parse("source other.sdc\nset_output_delay 1 [get_ports p]\n"
                        "set_case_analysis 0 [get_ports d]\nif {1} {}\nproc f {} {}\nexpr 1+1\n", "f");
    CHECK(hasProblem(c, "source: file could not be read"));
    // Output delay is rejected for missing -clock; binary case analysis is recorded, not applied.
    CHECK(hasProblem(c, "set_output_delay: missing -clock"));
    CHECK(c.outputDelays.empty()); CHECK_EQ(c.caseAnalyses.size(), 1u);
    CHECK_EQ(c.unsupported.size(), 2u);
    CHECK(c.clocks.empty());
}

TEST(sdc_malformed_create_clock_without_period) {
    auto c = sdc::parse("create_clock -name clk [get_ports clk]\n", "f");
    CHECK(hasProblem(c, "missing required -period"));
    CHECK(c.clocks.empty());
    CHECK_EQ(c.problems.size(), 1u);
    if (c.problems.size() == 1) CHECK_EQ(c.problems[0].loc.line, 1u);
}

TEST(sdc_malformed_missing_option_value) {
    auto c = sdc::parse("create_clock -name clk -period\n", "f");
    CHECK(hasProblem(c, "option '-period' of create_clock is missing its value"));
    CHECK(c.clocks.empty());
}

TEST(sdc_malformed_bad_numeric_value) {
    auto c = sdc::parse("create_clock -period fast [get_ports a]\n"
                        "set_max_delay abc -to [get_ports y]\n"
                        "set_multicycle_path 1.5 -from [get_clocks a]\n",
                        "f");
    CHECK(hasProblem(c, "invalid -period 'fast'"));
    CHECK(hasProblem(c, "invalid delay value 'abc'"));
    CHECK(hasProblem(c, "invalid multiplier '1.5'"));
    CHECK(c.clocks.empty());
    CHECK(c.exceptions.empty());
}

TEST(sdc_malformed_unknown_option_and_missing_value) {
    auto c = sdc::parse("set_false_path -bogus -from\n", "f");
    CHECK(hasProblem(c, "unknown option '-bogus'"));
    CHECK(hasProblem(c, "option '-from' of set_false_path is missing its value"));
    CHECK(c.exceptions.empty());
}

TEST(sdc_problems_point_at_their_command) {
    auto c = sdc::parse("set a 1\n"
                        "create_clock -name x\n"
                        "set_clock_groups -group clk_a\n",
                        "f");
    // Line 2: missing -period. Line 3: missing kind and fewer than two -group lists.
    CHECK_EQ(c.problems.size(), 3u);
    for (const auto& p : c.problems) CHECK(p.loc.line == 2u || p.loc.line == 3u);
}

TEST(sdc_empty_input) {
    auto c = sdc::parse("", "empty.sdc");
    CHECK(c.clocks.empty());
    CHECK(c.clockGroups.empty());
    CHECK(c.exceptions.empty());
    CHECK(c.unsupported.empty());
    CHECK(c.problems.empty());
    auto w = sdc::parse("  \n\n\t\n;;\n# only a comment\n", "ws.sdc");
    CHECK(w.clocks.empty() && w.unsupported.empty() && w.problems.empty());
}

TEST(sdc_garbage_input_does_not_crash) {
    const char* fixed[] = {
        "{", "}", "[", "]", "\"", "\\", "[[", "]]", "{{", "}}", "\\\n", "\\", "create_clock [",
        "set_clock_groups -group", "create_clock -name {", "set_false_path -from \"abc",
        "[get_ports", "$$$", "create_clock -period 1e999 [get_ports a]", "create_clock -period nan [get_ports a]",
        "set_false_path -through [[[[]]]]", "create_clock -period 1 [get_ports {a b", "[]", "[;]", "{}{}{}",
        "create_generated_clock -divide_by 0 -source [get_ports] [get_pins]",
        "set_clock_groups -asynchronous -group [get_clocks] -group [foo bar]",
        "set_max_delay 1 -from [get_ports \"unterminated",
    };
    for (const char* s : fixed) {
        auto c = sdc::parse(s, "junk");
        (void)c;
    }
    // Binary junk, including NUL bytes and every byte value, repeated.
    std::string junk;
    for (int r = 0; r < 64; ++r) {
        for (int b = 0; b < 256; ++b) junk.push_back(static_cast<char>(b));
    }
    auto c = sdc::parse(junk, "bin");
    (void)c;
    CHECK(true);
}

TEST(sdc_unbalanced_brace_is_problem) {
    auto c = sdc::parse("create_clock -name {clk -period 10\nset_false_path -to [get_ports a]\n", "f");
    CHECK(hasProblem(c, "unbalanced '{'"));
    // The open brace swallows the rest of the input, so nothing after it is a separate command.
    CHECK(c.unsupported.empty());
    CHECK(c.exceptions.empty());
}

TEST(sdc_unbalanced_bracket_is_problem) {
    auto c = sdc::parse("create_clock -period 1 [get_ports a\nset x 1\n", "f");
    CHECK(hasProblem(c, "unbalanced '['"));
    CHECK(c.clocks.empty());
}

TEST(sdc_trailing_backslash_does_not_crash) {
    auto c = sdc::parse("create_clock -period 2 [get_ports a] \\", "f");
    CHECK(c.clocks.empty());
    CHECK(hasProblem(c, "backslash escapes in list"));
    auto u = sdc::parse("set_output_delay 1 \\", "f");
    CHECK(!u.problems.empty()); CHECK(u.outputDelays.empty());
}

TEST(sdc_line_and_column_numbers) {
    const char* text =
        "set a 1\n"
        "  create_clock -name a -period 5 [get_ports a]\n"
        "set_load 1 [get_ports b]; create_clock -name z -period 1\n";
    auto c = sdc::parse(text, "pos.sdc");
    CHECK_EQ(c.unsupported.size(), 1u);
    if (c.unsupported.size() == 1) {
        CHECK_EQ(c.unsupported[0].loc.line, 3u);
        CHECK_EQ(c.unsupported[0].loc.column, 1u);
    }
    CHECK_EQ(c.clocks.size(), 2u);
    if (c.clocks.size() == 2) {
        CHECK_EQ(c.clocks[0].loc.line, 2u);
        CHECK_EQ(c.clocks[0].loc.column, 3u);
        CHECK_EQ(c.clocks[1].loc.line, 3u);
        CHECK_EQ(c.clocks[1].loc.column, 27u);   // after "set_load 1 [get_ports b]; "
    }
}

TEST(sdc_object_forms) {
    auto c = sdc::parse("set_false_path -from [all_clocks] -to [get_clocks -of_objects [get_pins u/CK]]\n"
                        "set_false_path -through [get_nets {n1 n2}] -to [get_cells core/*]\n",
                        "f");
    CHECK(c.problems.empty());
    CHECK_EQ(c.exceptions.size(), 2u);
    if (c.exceptions.size() != 2) return;
    std::vector<R> all = {R{"all_clocks", ""}};
    CHECK_EQ(c.exceptions[0].from, all);
    std::vector<R> of = {R{"get_clocks", "u/CK"}};
    CHECK_EQ(c.exceptions[0].to, of);
    std::vector<R> thr = {R{"get_nets", "n1"}, R{"get_nets", "n2"}};
    CHECK_EQ(c.exceptions[1].through, thr);
    std::vector<R> cells = {R{"get_cells", "core/*"}};
    CHECK_EQ(c.exceptions[1].to, cells);
}

TEST(sdc_unknown_object_command_is_problem) {
    auto c = sdc::parse("set_false_path -from [unknown a] -to [get_clocks]\n", "f");
    CHECK(hasProblem(c, "unsupported object command 'unknown'"));
    CHECK(c.exceptions.empty());
}

TEST(sdc_very_long_line) {
    std::string big = "create_clock -name c -period 1 [get_ports a";
    big.append(200000, 'b');
    big += "]";
    auto c = sdc::parse(big, "f");
    CHECK_EQ(c.clocks.size(), 1u);
    if (c.clocks.size() == 1) {
        CHECK_EQ(c.clocks[0].sources.size(), 1u);
        if (!c.clocks[0].sources.empty()) CHECK_EQ(c.clocks[0].sources[0].pattern.size(), 200001u);
    }
}

TEST(sdc_deep_nesting_does_not_crash) {
    std::string deep = "set_false_path -from ";
    deep.append(100000, '[');
    deep += "get_ports a";
    deep.append(100000, ']');
    auto c = sdc::parse(deep, "f");
    CHECK(c.exceptions.empty());
    CHECK(!c.problems.empty());
}

TEST(sdc_merge_keeps_everything) {
    auto a = sdc::parse("create_clock -name a -period 1 [get_ports a]\n", "a.sdc");
    auto b = sdc::parse("set x 1\nset_false_path -to [get_ports q]\n", "b.sdc");
    a.merge(std::move(b));
    CHECK_EQ(a.clocks.size(), 1u);
    CHECK(a.unsupported.empty());
    CHECK_EQ(a.exceptions.size(), 1u);
}

TEST(sdc_atomic_rejection_every_handler) {
    const char* commands[] = {
        "create_clock -name a -period bad [get_ports a]",
        "create_generated_clock -name b -source [get_ports a] -divide_by bad [get_ports b]",
        "set_clock_groups -asynchronous -group a -group [unknown b]",
        "set_false_path -from a -to [unknown b]",
        "set_multicycle_path bad -from a -to b",
        "set_max_delay bad -from a -to b",
        "set_min_delay bad -from a -to b",
        "set_input_delay -clock a bad [get_ports p]",
        "create_clock -period 1 [get_ports a", // scanner failure, not just handler failure
        "set_input_delay 1 [get_ports p]",
        "create_generated_clock -name b -source [get_ports a] -edges {1 bad 3} [get_ports b]",
        "set_input_delay -clock [unknown a] 1 [get_ports p]"
    };
    for (auto text : commands) {
        auto c = sdc::parse(text, "bad.sdc");
        CHECK(!c.problems.empty());
        CHECK(c.clocks.empty() && c.clockGroups.empty() && c.exceptions.empty() && c.inputDelays.empty());
        for (const auto& p : c.problems) { CHECK_EQ(p.loc.file, std::string("bad.sdc")); CHECK_EQ(p.loc.line, 1u); }
    }
}

TEST(sdc_variable_period_and_alias) {
    auto c = sdc::parse("set P 10\nset Q $P\ncreate_clock -period $Q [get_ports a]", "f");
    CHECK(c.problems.empty());
    CHECK_EQ(c.clocks.size(), 1u);
    if (!c.clocks.empty()) CHECK(c.clocks[0].periodNs == 10);
}

TEST(sdc_variable_forms_and_quoted_concatenation) {
    auto c = sdc::parse("set suffix a\nset P 10\ncreate_clock -name \"clk_${suffix}\" -period ${P} [get_ports clk_$suffix]", "f");
    CHECK(c.problems.empty());
    CHECK_EQ(c.clocks.size(), 1u);
    if (!c.clocks.empty()) { CHECK_EQ(c.clocks[0].name, std::string("clk_a")); CHECK_EQ(c.clocks[0].sources[0].pattern, std::string("clk_a")); }
}

TEST(sdc_braces_do_not_substitute) {
    auto c = sdc::parse("set P 10\ncreate_clock -period {$P} [get_ports a]", "f");
    CHECK(hasProblem(c, "invalid -period '$P'"));
    CHECK(c.clocks.empty());
}

TEST(sdc_undefined_variable_applies_nothing) {
    auto c = sdc::parse("create_clock -period $missing [get_ports a]", "f");
    CHECK(hasProblem(c, "undefined variable")); CHECK(c.clocks.empty());
}

TEST(sdc_foreach_clocks_and_body_provenance) {
    auto c = sdc::parse("set clocks {a b}\nforeach c $clocks {\n  create_clock -name $c -period 10 [get_ports $c]\n}\nset c\ncreate_clock -name last -period 2 [get_ports $c]", "f");
    CHECK(c.problems.empty() && c.unsupported.empty());
    CHECK_EQ(c.clocks.size(), 3u);
    if (c.clocks.size() == 3) {
        CHECK_EQ(c.clocks[0].loc.line, 3u); CHECK_EQ(c.clocks[0].loc.column, 3u);
        CHECK_EQ(c.clocks[1].name, std::string("b"));
        CHECK_EQ(c.clocks[2].sources[0].pattern, std::string("b"));
    }
}

TEST(sdc_foreach_iteration_limit) {
    std::string script = "foreach c {";
    for (int i = 0; i < 1000; ++i) script += "a ";
    auto boundary = sdc::parse(script + "} {create_clock -period 1 [get_ports $c]}", "f");
    CHECK(boundary.problems.empty()); CHECK_EQ(boundary.clocks.size(), 1000u);
    auto c = sdc::parse(script + "a} {create_clock -period 1 [get_ports $c]}", "f");
    CHECK(hasProblem(c, "iteration limit")); CHECK(c.clocks.empty());
}

TEST(sdc_expr_and_unsupported_bracket_disclosed) {
    for (auto command : {"unknown 10"}) {
        auto c = sdc::parse(std::string("create_clock -period [") + command + "] [get_ports a]", "f");
        CHECK(hasProblem(c, "unsupported object command")); CHECK(c.clocks.empty());
    }
}

TEST(sdc_expr_substitution) {
    auto c = sdc::parse("set p 10.0; create_clock -name a -period [expr {$p/2}] [get_ports a]", "f");
    CHECK(c.problems.empty()); CHECK_EQ(c.clocks.size(), 1u);
    if (!c.clocks.empty()) CHECK(c.clocks[0].periodNs == 5.0);
}

TEST(sdc_expr_refuses_values_tcl_would_compute_differently) {
    auto inexact = sdc::parse("create_clock -name a -period [expr {5/2}] [get_ports a]", "f");
    CHECK(hasProblem(inexact, "expr:")); CHECK_EQ(inexact.clocks.size(), 0u);
    auto exact = sdc::parse("create_clock -name a -period [expr {6/2}] [get_ports a]\ncreate_clock -name b -period [expr {5.0/2}] [get_ports b]", "f");
    CHECK(!hasProblem(exact, "expr:")); CHECK_EQ(exact.clocks.size(), 2u);
    CHECK_EQ(exact.clocks[0].periodNs, 3.0); CHECK_EQ(exact.clocks[1].periodNs, 2.5);
    auto modulo = sdc::parse("create_clock -name a -period [expr {5.5 % 2}] [get_ports a]", "f");
    CHECK(hasProblem(modulo, "expr:"));
    auto small = sdc::parse("create_clock -name a -period [expr {0.0000001*2}] [get_ports a]", "f");
    CHECK_EQ(small.clocks.size(), 1u); CHECK(small.clocks[0].periodNs > 0);   // no rounding to six decimals
    auto deep = sdc::parse("create_clock -name a -period [expr {" + std::string(100000, '(') + "1" + std::string(100000, ')') + "}] [get_ports a]", "f");
    CHECK(hasProblem(deep, "expr:"));   // refused, not a stack overflow
}

TEST(sdc_expr_refuses_inexact_integers_and_variable_syntax) {
    auto big = sdc::parse("if {9007199254740993 == 9007199254740992} {create_clock -name a -period 1 [get_ports a]}", "f");
    CHECK(hasProblem(big, "expr:")); CHECK_EQ(big.clocks.size(), 0u);
    auto grown = sdc::parse("create_clock -name a -period [expr {9007199254740991 + 5}] [get_ports a]", "f");
    CHECK(hasProblem(grown, "expr:")); CHECK_EQ(grown.clocks.size(), 0u);
    auto syntax = sdc::parse("set v {1 + 1}\nif {$v * 0 == 1} {create_clock -name a -period 1 [get_ports a]}", "f");
    CHECK(hasProblem(syntax, "plain number")); CHECK_EQ(syntax.clocks.size(), 0u);
    auto negative = sdc::parse("set v -2\ncreate_clock -name a -period [expr {$v * -3}] [get_ports a]", "f");
    CHECK_EQ(negative.clocks.size(), 1u);
    if (!negative.clocks.empty()) CHECK_EQ(negative.clocks[0].periodNs, 6.0);
}

TEST(sdc_expr_float_result_stays_a_float) {
    auto c = sdc::parse("set v [expr {2.0}]\nif {$v % 2 == 0} {create_clock -name a -period 1 [get_ports a]}", "f");
    CHECK(hasProblem(c, "expr:")); CHECK_EQ(c.clocks.size(), 0u);
}

TEST(sdc_lindex_is_not_failed_by_an_earlier_problem) {
    auto c = sdc::parse("create_clock -period [expr {1/0}] [get_ports a]\ncreate_clock -name [lindex {x y} 1] -period 2 [get_ports b]", "f");
    CHECK_EQ(c.clocks.size(), 1u);
    if (!c.clocks.empty()) CHECK(c.clocks[0].name == "y");
}

TEST(sdc_expr_failure_is_atomic) {
    auto c = sdc::parse("create_clock -period [expr {1/0}] [get_ports a]\ncreate_clock -period 2 [get_ports b]", "f");
    CHECK(hasProblem(c, "expr:")); CHECK_EQ(c.clocks.size(), 1u);
    if (!c.clocks.empty()) CHECK(c.clocks[0].periodNs == 2.0);
}

TEST(sdc_lindex_substitution) {
    auto c = sdc::parse("set xs {1 2 3}\ncreate_clock -period [lindex $xs end] [get_ports a]", "f");
    CHECK(c.problems.empty()); CHECK_EQ(c.clocks.size(), 1u);
    if (!c.clocks.empty()) CHECK(c.clocks[0].periodNs == 3.0);
}

TEST(sdc_lindex_failure_is_atomic) {
    auto c = sdc::parse("create_clock -period [lindex {1 2} 4] [get_ports a]\ncreate_clock -period 2 [get_ports b]", "f");
    CHECK(hasProblem(c, "lindex:")); CHECK_EQ(c.clocks.size(), 1u);
    if (!c.clocks.empty()) CHECK(c.clocks[0].periodNs == 2.0);
}

TEST(sdc_set_read_substitution) {
    auto c = sdc::parse("set P 10\ncreate_clock -period [set P] [get_ports a]", "f");
    CHECK(c.problems.empty()); CHECK_EQ(c.clocks.size(), 1u);
}

TEST(sdc_foreach_multiple_variables_disclosed) {
    auto c = sdc::parse("foreach {a b} {x y} {create_clock -period 1 [get_ports $a]}", "f");
    CHECK(hasProblem(c, "one scalar variable")); CHECK(c.clocks.empty());
}

TEST(sdc_substituted_text_is_not_evaluated_again) {
    auto c = sdc::parse("set P {$missing}\ncreate_clock -period $P [get_ports a]", "f");
    CHECK(hasProblem(c, "invalid -period '$missing'")); CHECK(c.clocks.empty());
    auto literal = sdc::parse("set pin {a[0]}\ncreate_clock -period 1 [get_ports $pin]", "f");
    CHECK(literal.problems.empty()); CHECK_EQ(literal.clocks.size(), 1u);
    if (!literal.clocks.empty()) CHECK_EQ(literal.clocks[0].sources[0].pattern, std::string("a[0]"));
}

TEST(sdc_failed_assignment_does_not_reuse_stale_value) {
    auto c = sdc::parse("set P 10\nset P [expr nope]\ncreate_clock -period $P [get_ports a]", "f");
    CHECK(hasProblem(c, "expr:"));
    CHECK(hasProblem(c, "undefined variable")); CHECK(c.clocks.empty());
}

TEST(sdc_foreach_failure_is_atomic) {
    auto c = sdc::parse("foreach c {10 bad} {create_clock -name c -period $c [get_ports c]}", "f");
    CHECK(hasProblem(c, "invalid -period")); CHECK(c.clocks.empty());
}

TEST(sdc_if_taken_else_elseif_and_source_options) {
    auto c = sdc::parse("set choose 1\n"
                        "if {$choose} {\n create_clock -name picked -period 2 [get_ports a]\n} elseif {1} {\n create_clock -name wrong -period 3 [get_ports b]\n} else {\n create_clock -name wrong2 -period 4 [get_ports c]\n}\n"
                        "if {0} {create_clock -period 5 [get_ports d]} elseif {1} {create_clock -name elseif_picked -period 6 [get_ports f]} else {create_clock -period 7 [get_ports e]}\n"
                        "if {0} {create_clock -period 8 [get_ports d]} else {create_clock -period 9 [get_ports e]}\n",
                        "f");
    CHECK(c.problems.empty()); CHECK(c.unsupported.empty()); CHECK_EQ(c.clocks.size(), 3u);
    if (c.clocks.size() == 3) { CHECK_EQ(c.clocks[0].name, std::string("picked")); CHECK_EQ(c.clocks[0].loc.line, 3u); CHECK_EQ(c.clocks[1].name, std::string("elseif_picked")); CHECK_EQ(c.clocks[2].sources[0].pattern, std::string("e")); }
    auto sourced = sdc::parse("source -echo -verbose missing.sdc", "f");
    CHECK(hasProblem(sourced, "file could not be read"));
    CHECK(!hasProblem(sourced, "only -echo and -verbose"));
}

TEST(sdc_if_undecidable_is_atomic_and_invalidates_variables) {
    auto c = sdc::parse("set maybe 7\nif {$unset} {set maybe 9}\ncreate_clock -period $maybe [get_ports a]\n", "f");
    CHECK(hasProblem(c, "expr: undefined variable"));
    CHECK(hasProblem(c, "branch outcome is unknown"));
    CHECK(hasProblem(c, "undefined variable 'maybe'"));
    CHECK(c.clocks.empty());
}

TEST(sdc_braced_variable_name_keeps_script_delimiters_literal) {
    auto c = sdc::parse("set {P;x} 10\ncreate_clock -period ${P;x} [get_ports a]", "f");
    CHECK(c.problems.empty()); CHECK_EQ(c.clocks.size(), 1u);
    if (!c.clocks.empty()) CHECK(c.clocks[0].periodNs == 10);
}

TEST(sdc_source_relative_shared_variable_and_provenance) {
    auto dir = std::filesystem::temp_directory_path() / "sverity_sdc_source_test";
    std::filesystem::create_directories(dir / "sub");
    { std::ofstream f(dir / "sub" / "child.sdc"); f << "\ncreate_clock -name $name -period 10 [get_ports clk]\n"; }
    auto c = sdc::parse("set name shared\nsource sub/child.sdc\n", (dir / "top.sdc").string());
    CHECK(c.problems.empty());
    CHECK_EQ(c.clocks.size(), 1u);
    if (!c.clocks.empty()) { CHECK_EQ(c.clocks[0].name, "shared"); CHECK_EQ(c.clocks[0].loc.line, 2u); CHECK_EQ(c.clocks[0].loc.file, (dir / "sub" / "child.sdc").string()); }
    std::filesystem::remove_all(dir);
}

TEST(sdc_source_cycle_disclosed) {
    auto dir = std::filesystem::temp_directory_path() / "sverity_sdc_cycle_test";
    std::filesystem::create_directories(dir);
    { std::ofstream f(dir / "child.sdc"); f << "source top.sdc\n"; }
    auto c = sdc::parse("source child.sdc\n", (dir / "top.sdc").string());
    CHECK(hasProblem(c, "cycle detected"));
    std::filesystem::remove_all(dir);
}

TEST(sdc_source_depth_limit_disclosed) {
    auto dir = std::filesystem::temp_directory_path() / "sverity_sdc_depth_test";
    std::filesystem::create_directories(dir);
    for (int i = 1; i <= 65; ++i) {
        std::ofstream f(dir / (std::to_string(i) + ".sdc"));
        f << "source " << i + 1 << ".sdc\n";
    }
    auto c = sdc::parse("source 1.sdc\n", (dir / "top.sdc").string());
    CHECK(hasProblem(c, "depth limit exceeded"));
    std::filesystem::remove_all(dir);
}

TEST(sdc_source_options_disclosed) {
    auto c = sdc::parse("set p 10\nsource -encoding utf-8 child.sdc\ncreate_clock -period $p [get_ports clk]\n", "top.sdc");
    CHECK(hasProblem(c, "only -echo and -verbose options are accepted"));
    CHECK(c.sourcedFiles.empty()); CHECK(c.clocks.empty());
    CHECK(hasProblem(c, "undefined variable"));
}

TEST(sdc_all_inputs_options_reject_whole_command) {
    auto c = sdc::parse("set_input_delay -clock clk 1 [all_inputs -clock clk]\n", "f");
    CHECK(hasProblem(c, "all_inputs: options")); CHECK(c.inputDelays.empty());
}

TEST(sdc_all_inputs_no_clocks_is_a_port_collection_only) {
    auto c = sdc::parse("set_input_delay -clock clk 1 [all_inputs -no_clocks]\n", "f");
    CHECK(c.problems.empty()); CHECK_EQ(c.inputDelays.size(), 1u);
    CHECK(hasProblem(sdc::parse("create_clock -period 10 [all_inputs -no_clocks]\n", "f"), "defined by the clocks"));
    CHECK(hasProblem(sdc::parse("set_clock_groups -asynchronous -group [all_inputs -no_clocks] -group clk\n", "f"), "all_inputs"));
    CHECK(hasProblem(sdc::parse("set_case_analysis 0 [all_inputs -no_clocks]\n", "f"), "expected ports"));
}

TEST(sdc_all_inputs_clock_position_disclosed) {
    auto c = sdc::parse("set_clock_groups -asynchronous -group [all_inputs] -group clk\n", "f");
    CHECK(hasProblem(c, "expected clocks")); CHECK(c.clockGroups.empty());
}

TEST(sdc_output_delay_recorded) {
    auto c = sdc::parse("set_output_delay -clock [get_clocks clk] -1.5 [get_ports {q r}]\n", "f");
    CHECK(c.problems.empty()); CHECK_EQ(c.outputDelays.size(), 1u);
    if (c.outputDelays.empty()) return;
    CHECK_EQ(c.outputDelays[0].clock, "clk"); CHECK_EQ(c.outputDelays[0].valueText, "-1.5");
    CHECK_EQ(c.outputDelays[0].ports.size(), 2u); CHECK(!c.outputDelays[0].text.empty());
    CHECK(c.inputDelays.empty());
}
TEST(sdc_output_delay_invalid_atomic) {
    for (auto text : {"set_output_delay 1 [get_ports q]", "set_output_delay -clock c nope [get_ports q]",
                      "set_output_delay -clock c 1 [get_nets q]", "set_output_delay -clock c -rise 1 [get_ports q]"}) {
        auto c = sdc::parse(text, "f");
        CHECK(!c.problems.empty()); CHECK(c.outputDelays.empty());
    }
}
TEST(sdc_case_analysis_recorded_not_applied) {
    auto c = sdc::parse("set_case_analysis 0 [get_ports sel]\nset_case_analysis 1 [get_nets top.mode]", "f");
    CHECK(c.problems.empty()); CHECK_EQ(c.caseAnalyses.size(), 2u);
    if (c.caseAnalyses.size() != 2) return;
    CHECK_EQ(c.caseAnalyses[0].value, 0); CHECK_EQ(c.caseAnalyses[1].value, 1);
    CHECK_EQ(c.caseAnalyses[1].objects[0].kind, "get_nets");
}
TEST(sdc_case_analysis_value_rejected) {
    for (auto value : {"1'bx", "rising", "falling", "2", "1'b0"}) {
        auto c = sdc::parse(std::string("set_case_analysis ") + value + " [get_ports sel]", "f");
        CHECK(hasProblem(c, "only literal 0 or 1")); CHECK(c.caseAnalyses.empty());
    }
}
TEST(sdc_recorded_constraints_loop_rollback) {
    auto c = sdc::parse("foreach p {q r} {set_output_delay -clock c 1 [get_ports $p]; set_case_analysis 0 [get_ports sel]; unknown_command}", "f");
    CHECK(c.outputDelays.empty()); CHECK(c.caseAnalyses.empty()); CHECK(!c.unsupported.empty());
}
TEST(sdc_recorded_constraints_merge) {
    auto c = sdc::parse("set_output_delay -clock c 1 q", "f");
    c.merge(sdc::parse("set_case_analysis 1 sel", "g"));
    CHECK_EQ(c.outputDelays.size(), 1u); CHECK_EQ(c.caseAnalyses.size(), 1u);
}
