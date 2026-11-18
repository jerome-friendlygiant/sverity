"""The mutation operators of tools/dev/mutation_recall.py on small modules (no binary, no RTL from any project): each
operator offers a site only when its precondition holds, edits never add or remove a line, and scoring counts only a
finding that is new against the baseline at the site or naming the mutated signal. Usage: test_mutation_recall.py"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools/dev"))
import mutation_recall as M  # noqa: E402

PORTS = {"leaf": {"a_i": "input", "y_o": "output"}}


def sites(text, op, known=("leaf",), ports=PORTS, enums=None):
    src = M.Source(text)
    out = []
    for module in src.modules:
        out += [s for s in M.candidate_sites(src, module, set(known), ports, enums or M.enum_table([src])) if s.op == op]
    return src, out


def mutate(text, op, **kw):
    src, found = sites(text, op, **kw)
    return [M.apply_edits(text, s.edits) for s in found]


COMB = """module m (input logic clk_i, input logic rst_ni, input logic [3:0] d_i, input logic en_i, output logic [3:0] q_o);
  typedef enum logic [1:0] {A, B, C} st_e;
  st_e st_q, st_d;
  logic [3:0] w, v;
  logic one;
  assign w = d_i;
  assign q_o = v;
  assign v = w;
  always_comb begin
    st_d = st_q;
    one = 1'b0;
    if (en_i) begin
      one = 1'b1;
    end
    unique case (st_q)
      A: st_d = B;
      B: st_d = C;
      default: st_d = A;
    endcase
  end
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      st_q <= A;
    end else begin
      st_q <= st_d;
    end
  end
endmodule
"""


class Masking(unittest.TestCase):
    def test_comments_strings_and_conditional_regions_are_blanked_with_newlines_kept(self):
        text = 'a // x\n"s" /* y\nz */ `ifdef Q\nb\n`endif\nc `MACRO(1, 2) d\n'
        masked = M.mask_directives(M.mask_comments(text))
        self.assertEqual(masked.count("\n"), text.count("\n"))
        self.assertEqual(len(masked), len(text))
        self.assertEqual([t.text for t in M.tokenize(masked)], ['a', '"', '"', 'c', 'd'])

    def test_an_edit_that_changes_the_line_count_is_refused(self):
        with self.assertRaises(ValueError):
            M.apply_edits("a\nb", [(1, 2, "")])


class Operators(unittest.TestCase):
    def test_widen_truncate_doubles_the_right_side_of_an_equal_width_assignment(self):
        out = mutate(COMB, "LC02-widen-truncate")
        self.assertTrue(any("assign w = {d_i, d_i};" in o for o in out), out)
        self.assertTrue(any("assign v = {w, w};" in o for o in out), out)

    def test_index_past_the_end_uses_the_declared_width(self):
        text = "module m (input logic [W-1:0] x_i, output logic y_o);\n  assign y_o = x_i[2];\nendmodule\n"
        self.assertEqual(mutate(text, "LC04-index-past-end"), [text.replace("[2]", "[W]")])
        text = text.replace("[W-1:0]", "[7:0]")
        self.assertEqual(mutate(text, "LC04-index-past-end"), [text.replace("[2]", "[8]")])

    def test_typo_net_needs_no_default_nettype_none(self):
        text = "module m (input logic a_i, output logic y_o);\n  logic s;\n  leaf u (.a_i(s), .y_o(y_o));\nendmodule\n"
        out = mutate(text, "LC05-typo-net")
        self.assertEqual(len(out), 2)                                   # a local signal and a port, one site each
        self.assertTrue(any(".a_i(s_tpo)" in o for o in out))
        self.assertEqual(mutate("`default_nettype none\n" + text, "LC05-typo-net"), [])

    def test_deleting_the_only_driver_needs_a_read_and_no_other_write(self):
        out = mutate(COMB, "LC06-delete-driver")
        self.assertEqual(len(out), 2)                                   # w (read by v) and v (read by q_o)
        written = COMB.replace("  assign q_o = v;\n", "  assign q_o = v;\n  always_comb w[0] = 1'b0;\n")
        names = {s.signals[0] for s in sites(written, "LC06-delete-driver")[1]}
        self.assertNotIn("w", names)
        unread = "module m (input logic a_i);\n  logic x;\n  assign x = a_i;\nendmodule\n"
        self.assertEqual(mutate(unread, "LC06-delete-driver"), [])

    def test_second_driver_turns_a_logic_into_a_wire(self):
        out = mutate(COMB, "LC07-second-driver")
        self.assertEqual([o for o in out if "assign w = ~(d_i);" in o], [])   # `logic [3:0] w, v;` declares two names
        single = "module m (input logic a_i, output logic y_o);\n  logic x;\n  assign x = a_i;\n  assign y_o = x;\nendmodule\n"
        out = mutate(single, "LC07-second-driver")
        self.assertEqual(len(out), 1)
        self.assertIn("wire x;", out[0])
        self.assertIn("assign x = ~(a_i);", out[0])

    def test_empty_input_only_for_an_input_port(self):
        text = "module m (input logic a, output logic y);\n  leaf u (.a_i(a), .y_o(y));\nendmodule\n"
        out = mutate(text, "LC08-empty-input")
        self.assertEqual(len(out), 1)
        self.assertIn(".a_i(", out[0])
        self.assertIn(".y_o(y)", out[0])
        self.assertNotIn(".a_i(a)", out[0])

    def test_self_xor_closes_a_loop_through_the_assigned_name(self):
        out = mutate(COMB, "LC10-self-xor")
        self.assertTrue(any("assign w = (d_i) ^ w;" in o for o in out), out)

    def test_delete_default_needs_a_proof_that_a_path_leaves_the_variable_unassigned(self):
        out = mutate(COMB, "LC11-delete-default")
        self.assertEqual(len(out), 1)                                   # `one` only: `st_d` is also assigned in every case branch
        self.assertNotIn("one = 1'b0;", out[0])
        exhaustive = COMB.replace("      one = 1'b1;\n", "      one = 1'b1;\n    end else begin\n      one = 1'b0;\n")
        self.assertEqual(mutate(exhaustive, "LC11-delete-default"), [])

    def test_blocking_and_nonblocking_swaps(self):
        text = ("module m (input logic clk_i, input logic d_i, output logic a_q, output logic b_q);\n"
                "  always_ff @(posedge clk_i) begin\n    a_q <= d_i;\n    b_q <= a_q;\n  end\n"
                "  always_comb begin\n    b_q2 = d_i;\n  end\n  logic b_q2;\nendmodule\n")
        out = mutate(text, "LC12-nonblocking-to-blocking")
        self.assertEqual(len(out), 1)
        self.assertIn("a_q = d_i;", out[0])
        self.assertEqual(len(mutate(text, "LC12-blocking-to-nonblocking")), 1)

    def test_deleting_a_case_default_needs_an_uncovered_enumerator(self):
        out = mutate(COMB, "LC13-delete-case-default")
        self.assertEqual(len(out), 1)                                   # A and B are labels, C is not
        self.assertNotIn("default", out[0])
        full = COMB.replace("      default: st_d = A;", "      C: st_d = A;\n      default: st_d = A;")
        self.assertEqual(mutate(full, "LC13-delete-case-default"), [])

    def test_x_constant_delay_and_clock_as_data(self):
        self.assertTrue(any("one = 'x;" in o for o in mutate(COMB, "LC14-constant-to-x")))
        self.assertTrue(any("assign #1 w" in o for o in mutate(COMB, "LC15-delay")))
        self.assertTrue(any("assign v = (w) & clk_i;" in o for o in mutate(COMB.replace("logic [3:0] w, v;", "logic w, v;"), "LC17-clock-as-data")))
        self.assertEqual(mutate(COMB, "LC17-clock-as-data"), [])        # w and v are 4 bits wide

    def test_reset_value_becomes_a_data_input_of_the_same_shape(self):
        text = ("module m (input logic clk_i, input logic rst_ni, input logic d_i, output logic q_o);\n"
                "  always_ff @(posedge clk_i or negedge rst_ni) begin\n    if (!rst_ni) begin\n      q_o <= 1'b0;\n"
                "    end else begin\n      q_o <= d_i;\n    end\n  end\nendmodule\n")
        out = mutate(text, "LC16-reset-value-from-data")
        self.assertEqual(len(out), 1)
        self.assertIn("q_o <= d_i;", out[0].split("end else")[0])

    def test_constant_comparison_wraps_a_statement_in_a_block(self):
        out = mutate(COMB, "LC21-constant-comparison")
        self.assertTrue(any("if (d_i < 0) one = 1'b0;" in o for o in out), out)

    def test_translate_off_wraps_one_assignment_on_its_own_line(self):
        out = mutate(COMB, "LC25-translate-off")
        self.assertTrue(any("/* synopsys translate_off */ one = 1'b0; /* synopsys translate_on */" in o for o in out))

    def test_every_site_keeps_the_line_count(self):
        src = M.Source(COMB)
        for module in src.modules:
            for s in M.candidate_sites(src, module, {"leaf"}, PORTS, M.enum_table([src])):
                self.assertEqual(M.apply_edits(COMB, s.edits).count("\n"), COMB.count("\n"), s.op)


def diag(rule, line, severity="WARNING", confidence="established", disposition="REVIEW_REQUIRED", file="../x/src/core/rtl/a.sv",
         message="", signals=()):
    return {"rule_id": rule, "severity": severity, "confidence": confidence, "disposition": disposition, "message": message,
            "signals": list(signals), "location": {"file": file, "line": line}}


class Scoring(unittest.TestCase):
    MUTANT = {"file_key": "core/rtl/a.sv", "lines": [10], "signals": ["sig"], "cls": "LC06"}

    def test_a_finding_that_the_baseline_already_has_is_not_new(self):
        base = {"diagnostics": [diag("LNT-008", 11, file="../y/src/core/rtl/a.sv", message="x")]}
        report = {"diagnostics": [diag("LNT-008", 12, message="x"), diag("LNT-009", 11, message="x"), diag("LNT-008", 12, message="y")]}
        self.assertEqual([(d["rule_id"], d["message"]) for d in M.new_findings(report, base)], [("LNT-009", "x"), ("LNT-008", "y")])

    def test_gating_needs_error_or_an_established_unwaived_warning(self):
        self.assertTrue(M.gating(diag("R", 1)))
        self.assertTrue(M.gating(diag("R", 1, severity="ERROR", confidence="unresolved")))
        self.assertFalse(M.gating(diag("R", 1, confidence="unresolved")))
        self.assertFalse(M.gating(diag("R", 1, disposition="WAIVED")))
        self.assertFalse(M.gating(diag("R", 1, disposition="OBLIGATION", confidence="heuristic")))
        self.assertFalse(M.gating(diag("R", 1, severity="INFO")))

    def test_outcomes_follow_the_site_the_signal_and_the_gate(self):
        self.assertEqual(M.score(self.MUTANT, [diag("LNT-008", 11)])[0], "CAUGHT")
        self.assertEqual(M.score(self.MUTANT, [diag("LNT-008", 40, message="'sig' is undriven")])[0], "CAUGHT")
        self.assertEqual(M.score(self.MUTANT, [diag("LNT-008", 40, message="'other'")])[0], "MISSED")
        self.assertEqual(M.score(self.MUTANT, [diag("LNT-008", 10, file="../x/src/core/rtl/b.sv")])[0], "MISSED")
        self.assertEqual(M.score(self.MUTANT, [diag("LNT-008", 10, severity="INFO")])[0], "CAUGHT_NONGATING")
        self.assertEqual(M.score(self.MUTANT, [diag("LNT-008", 10, disposition="WAIVED")])[0], "CAUGHT_WAIVED")

    def test_a_solo_class_takes_a_new_finding_of_its_class_anywhere(self):
        mutant = dict(self.MUTANT, cls="LC17", signals=["clk_i"])
        elsewhere = diag("LNT-027", 3, file="../x/src/core/rtl/top.sv")
        self.assertEqual(M.score(mutant, [elsewhere])[0], "CAUGHT")
        self.assertEqual(M.score(dict(mutant, cls="LC06"), [elsewhere])[0], "MISSED")

    def test_solo_mutants_never_share_a_run(self):
        a = {"file": "a", "cls": "LC17"}
        b = {"file": "b", "cls": "LC17"}
        c = {"file": "c", "cls": "LC06"}
        self.assertEqual(M.batches_of([a, b, c]), [[a], [b], [c]])

    def test_wilson_interval_brackets_the_rate(self):
        lo, hi = M.wilson(9, 10)
        self.assertTrue(lo < 0.9 < hi < 1.0)
        self.assertEqual(M.wilson(0, 0), (0.0, 1.0))


if __name__ == "__main__":
    unittest.main()
