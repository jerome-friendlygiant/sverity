#include <string>
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
    CHECK_EQ(c.unsupported.size(), 1u);
    if (c.unsupported.size() == 1) {
        // Continuations are joined with a single space, so the text reads as one line.
        CHECK_EQ(c.unsupported[0].text, std::string("set_output_delay 1.2 -clock clk_core [get_ports din]"));
        CHECK_EQ(c.unsupported[0].loc.line, 4u);
    }
}

TEST(sdc_clock_groups_async_two_groups) {
    const char* text =
        "set_clock_groups -asynchronous \\\n"
        "  -group [get_clocks clk_a] \\\n"
        "  -group {clk_b [get_clocks {clk_c clk_d}]}\n";
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
    std::vector<R> g1 = {R{"", "clk_b"}, R{"get_clocks", "clk_c"}, R{"get_clocks", "clk_d"}};
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
    CHECK(c.clockGroups.size() == 1);
    CHECK(c.clockGroups.size() == 1 && c.clockGroups[0].kind == sdc::ClockGroups::Kind::Unspecified);
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
    CHECK_EQ(c.clocks.size(), 1u);
    if (c.clocks.size() == 1) CHECK(c.clocks[0].generated);
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
    CHECK_EQ(c.unsupported.size(), 1u);
    if (c.unsupported.size() == 1) CHECK_EQ(c.unsupported[0].command, std::string("set"));
}

TEST(sdc_unsupported_commands_recorded) {
    const char* text =
        "set_output_delay 2.0 -clock clk [get_ports din]\n"
        "set x 5\n"
        "if {$x} { create_clock -period 1 }\n"
        "foreach a {1 2} { puts $a }\n"
        "proc foo {} { return 1 }\n"
        "set_load 0.1 [all_outputs]\n";
    auto c = sdc::parse(text, "f");
    CHECK(c.problems.empty());
    CHECK(c.clocks.empty());   // the create_clock inside { } is not interpreted
    CHECK_EQ(c.unsupported.size(), 6u);
    if (c.unsupported.size() != 6) return;
    CHECK_EQ(c.unsupported[0].command, std::string("set_output_delay"));
    CHECK_EQ(c.unsupported[0].text, std::string("set_output_delay 2.0 -clock clk [get_ports din]"));
    CHECK_EQ(c.unsupported[1].command, std::string("set"));
    CHECK_EQ(c.unsupported[1].text, std::string("set x 5"));
    CHECK_EQ(c.unsupported[2].command, std::string("if"));
    CHECK_EQ(c.unsupported[2].text, std::string("if {$x} { create_clock -period 1 }"));
    CHECK_EQ(c.unsupported[5].command, std::string("set_load"));
    CHECK_EQ(c.unsupported[5].loc.line, 6u);
}

TEST(sdc_malformed_create_clock_without_period) {
    auto c = sdc::parse("create_clock -name clk [get_ports clk]\n", "f");
    CHECK(hasProblem(c, "missing required -period"));
    CHECK_EQ(c.clocks.size(), 1u);   // best-effort object is still recorded
    if (c.clocks.size() == 1) {
        CHECK_EQ(c.clocks[0].name, std::string("clk"));
        CHECK(c.clocks[0].periodNs == 0.0);
    }
    CHECK_EQ(c.problems.size(), 1u);
    if (c.problems.size() == 1) CHECK_EQ(c.problems[0].loc.line, 1u);
}

TEST(sdc_malformed_missing_option_value) {
    auto c = sdc::parse("create_clock -name clk -period\n", "f");
    CHECK(hasProblem(c, "option '-period' of create_clock is missing its value"));
    CHECK_EQ(c.clocks.size(), 1u);
}

TEST(sdc_malformed_bad_numeric_value) {
    auto c = sdc::parse("create_clock -period fast [get_ports a]\n"
                        "set_max_delay abc -to [get_ports y]\n"
                        "set_multicycle_path 1.5 -from [get_clocks a]\n",
                        "f");
    CHECK(hasProblem(c, "invalid -period 'fast'"));
    CHECK(hasProblem(c, "invalid delay value 'abc'"));
    CHECK(hasProblem(c, "invalid multiplier '1.5'"));
    CHECK_EQ(c.clocks.size(), 1u);
    CHECK_EQ(c.exceptions.size(), 2u);
    if (c.exceptions.size() == 2) CHECK_EQ(c.exceptions[0].valueText, std::string("abc"));
}

TEST(sdc_malformed_unknown_option_and_missing_value) {
    auto c = sdc::parse("set_false_path -bogus -from\n", "f");
    CHECK(hasProblem(c, "unknown option '-bogus'"));
    CHECK(hasProblem(c, "option '-from' of set_false_path is missing its value"));
    CHECK_EQ(c.exceptions.size(), 1u);
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
    CHECK_EQ(c.clocks.size(), 1u);
}

TEST(sdc_trailing_backslash_does_not_crash) {
    auto c = sdc::parse("create_clock -period 2 [get_ports a] \\", "f");
    CHECK_EQ(c.clocks.size(), 1u);
    auto u = sdc::parse("set_output_delay 1 \\", "f");
    CHECK_EQ(u.unsupported.size(), 1u);
    if (u.unsupported.size() == 1) CHECK_EQ(u.unsupported[0].text, std::string("set_output_delay 1 \\"));
}

TEST(sdc_line_and_column_numbers) {
    const char* text =
        "set a 1\n"
        "  create_clock -name a -period 5 [get_ports a]\n"
        "set_load 1 [get_ports b]; create_clock -name z -period 1\n";
    auto c = sdc::parse(text, "pos.sdc");
    CHECK_EQ(c.unsupported.size(), 2u);
    if (c.unsupported.size() == 2) {
        CHECK_EQ(c.unsupported[0].loc.file, std::string("pos.sdc"));
        CHECK_EQ(c.unsupported[0].loc.line, 1u);
        CHECK_EQ(c.unsupported[0].loc.column, 1u);
        CHECK_EQ(c.unsupported[1].loc.line, 3u);
        CHECK_EQ(c.unsupported[1].loc.column, 1u);
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
    auto c = sdc::parse("set_false_path -from [expr 1+1] -to [get_clocks]\n", "f");
    CHECK(hasProblem(c, "unsupported object command 'expr'"));
    CHECK(hasProblem(c, "'get_clocks' needs an object name"));
    CHECK_EQ(c.exceptions.size(), 1u);
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
    CHECK_EQ(c.exceptions.size(), 1u);
    CHECK(!c.problems.empty());
}

TEST(sdc_merge_keeps_everything) {
    auto a = sdc::parse("create_clock -name a -period 1 [get_ports a]\n", "a.sdc");
    auto b = sdc::parse("set x 1\nset_false_path -to [get_ports q]\n", "b.sdc");
    a.merge(std::move(b));
    CHECK_EQ(a.clocks.size(), 1u);
    CHECK_EQ(a.unsupported.size(), 1u);
    CHECK_EQ(a.exceptions.size(), 1u);
}
