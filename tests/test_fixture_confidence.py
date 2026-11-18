#!/usr/bin/env python3
"""Confidence assertions must reject mixed definite/unresolved findings."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

RUNNER = pathlib.Path(sys.argv.pop(1)).resolve()


class FixtureConfidence(unittest.TestCase):
    def run_case(self, body, line="3", rule="LNT-054", confidence=None):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            folder = root / rule
            folder.mkdir()
            directive = ("// expect-confidence: " + rule + " " + confidence if confidence else "// expect-unresolved: " + rule) + (" " + line if line else "")
            (folder / "confidence.sv").write_text("// sverity-args: --top top\n" + directive + "\n" + body)
            return subprocess.run([str(RUNNER), str(root), rule], capture_output=True, text=True, timeout=60)

    def test_mixed_confidence_at_same_location_fails(self):
        result = self.run_case("module top(input logic clk,rst_q,d, output logic y); logic a,b; "
                               "always_ff @(posedge clk) begin if(rst_q) a<=0; else a<=d; b<=d; end "
                               "always_comb if(a && b) y=1; else y=0; endmodule\n")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("different confidence beside expected unresolved", result.stdout)

    def test_unresolved_only_passes(self):
        result = self.run_case("module top(input logic clk,rst_q,d, output logic y); logic a; "
                               "always_ff @(posedge clk) if(rst_q) a<=0; else a<=d; "
                               "always_comb if(a) y=1; else y=0; endmodule\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_established_requirement_passes(self):
        result = self.run_case("module top(output logic q); assign q='x; endmodule\n", rule="LNT-039", confidence="established")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_wrong_confidence_requirement_fails(self):
        result = self.run_case("module top(output logic q); assign q='x; endmodule\n", rule="LNT-039", confidence="heuristic")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("different confidence", result.stdout)

    def split_location_case(self, line):
        return self.run_case("module top(input logic clk,rst_q,d, output logic y,z); logic a,b; "
                             "always_ff @(posedge clk) begin if(rst_q) a<=0; else a<=d; b<=d; end "
                             "always_comb if(a) y=1; else y=0;\n"
                             "always_comb if(b) z=1; else z=0; endmodule\n", line)

    def test_line_scope_excludes_other_locations(self):
        result = self.split_location_case("3")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_no_line_requires_all_matches_unresolved(self):
        result = self.split_location_case("")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("different confidence beside expected unresolved", result.stdout)


if __name__ == "__main__":
    unittest.main()
