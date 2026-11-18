"""The replay tools that read reports: the noise gate fails only when a count rises, and the Verilator differential
reconciles a warning with an SVerity finding at the same site (synthetic inputs). Usage: test_replay_tools.py"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools/dev"))
import noise_gate  # noqa: E402
import verilator_diff  # noqa: E402


def diag(severity, disposition="OPEN"):
    return {"rule_id": "LNT-001", "severity": severity, "disposition": disposition}


class NoiseGate(unittest.TestCase):
    def test_counts_separate_blocking_gaps_and_obligations(self):
        report = {"diagnostics": [diag("WARNING", "REVIEW_REQUIRED"), diag("ERROR"), diag("WARNING", "WAIVED"),
                                  diag("INFO"), diag("WARNING", "NOT_ANALYSED"), diag("WARNING", "OBLIGATION")]}
        self.assertEqual(noise_gate.counts(report), {"blocking": 2, "not_analysed": 1, "obligations": 1})

    def test_a_rise_fails_and_a_fall_only_notes(self):
        want = {"uart": {"blocking": 1, "not_analysed": 5, "obligations": 3}}
        failures, notes = noise_gate.compare(want, {"uart": {"blocking": 1, "not_analysed": 4, "obligations": 3}})
        self.assertEqual((failures, len(notes)), ([], 1))
        failures, _ = noise_gate.compare(want, {"uart": {"blocking": 2, "not_analysed": 5, "obligations": 3}})
        self.assertEqual(len(failures), 1)

    def test_a_missing_or_unexpected_ip_fails(self):
        want = {"uart": {"blocking": 0, "not_analysed": 0, "obligations": 0}}
        self.assertEqual(len(noise_gate.compare(want, {})[0]), 1)
        zero = {"blocking": 0, "not_analysed": 0, "obligations": 0}
        self.assertEqual(len(noise_gate.compare(want, {"uart": zero, "i2c": zero})[0]), 1)


class NoiseGateSites(unittest.TestCase):
    LEDGER = {("LNT-054", "a.sv", 10): "real defect", ("LNT-020", "b.sv", 5): "honest undecidable"}

    def test_a_site_outside_the_ledger_is_new_and_fails(self):
        failures, notes = noise_gate.compare_sites(self.LEDGER, set(self.LEDGER) | {("LNT-005", "c.sv", 3)})
        self.assertEqual((len(failures), notes), (1, []))
        self.assertIn("NEW", failures[0])

    def test_a_site_that_went_quiet_is_fixed_unless_it_is_a_real_defect(self):
        failures, notes = noise_gate.compare_sites(self.LEDGER, {("LNT-054", "a.sv", 10)})
        self.assertEqual((failures, len(notes)), ([], 1))
        failures, _ = noise_gate.compare_sites(self.LEDGER, {("LNT-020", "b.sv", 5)})
        self.assertEqual(len(failures), 1)
        self.assertIn("MUST-STAY", failures[0])

    def test_a_swap_of_one_site_for_another_is_not_hidden_by_equal_counts(self):
        failures, notes = noise_gate.compare_sites(self.LEDGER, {("LNT-054", "a.sv", 10), ("LNT-020", "b.sv", 6)})
        self.assertEqual((len(failures), len(notes)), (1, 1))

    def test_sites_are_the_blocking_and_not_analysed_findings(self):
        located = lambda severity, disposition, line: {"rule_id": "LNT-001", "severity": severity, "disposition": disposition,
                                                       "location": {"file": "x.sv", "line": line}}
        report = {"diagnostics": [located("WARNING", "REVIEW_REQUIRED", 1), located("INFO", "OPEN", 2), located("WARNING", "NOT_ANALYSED", 3),
                                  located("WARNING", "OBLIGATION", 4), located("WARNING", "WAIVED", 5)]}
        self.assertEqual(noise_gate.sites_of(report, lambda name: name), {("LNT-001", "x.sv", 1), ("LNT-001", "x.sv", 3)})


class Differential(unittest.TestCase):
    TEXT = ("%Warning-WIDTHTRUNC: src/a/rtl/dut.sv:10:5: Operator ASSIGN expects 4 bits on the Assign RHS\n"
            "                    : ... note: In instance 'dut'\n"
            "%Warning-WIDTHTRUNC: src/a/rtl/dut.sv:10:5: Operator ASSIGN expects 4 bits on the Assign RHS\n"
            "%Warning-UNUSEDSIGNAL: src/a/rtl/dut.sv:20:3: Signal is not used: 'x'\n"
            "%Warning-DECLFILENAME: src/a/rtl/dut.sv:1:8: Filename does not match\n")

    def rows(self, sverity):
        warnings = verilator_diff.parse_warnings(self.TEXT, pathlib.Path("."), lambda name, base: name)
        return warnings, verilator_diff.reconcile("dut", warnings, sverity, verilator_diff.catalog_rules_for_class())

    def test_a_repeated_site_is_one_warning(self):
        self.assertEqual(len(self.rows([])[0]), 3)

    def test_matched_other_near_silent_and_excluded(self):
        sv = [("LNT-004", "WARNING", "REVIEW_REQUIRED", "heuristic", "src/a/rtl/dut.sv", 10, "m", "sverity"),
              ("LNT-082", "INFO", "OPEN", "established", "src/a/rtl/dut.sv", 20, "m", "sverity")]
        status = {r["code"]: r["status"] for r in self.rows(sv)[1]}
        self.assertEqual(status, {"WIDTHTRUNC": "matched", "UNUSEDSIGNAL": "other", "DECLFILENAME": "excluded"})
        by = {r["code"]: r for r in self.rows([("LNT-004", "WARNING", "REVIEW_REQUIRED", "heuristic", "src/a/rtl/dut.sv", 10, "m", "sverity")])[1]}
        self.assertEqual((by["WIDTHTRUNC"]["matched_by"], by["WIDTHTRUNC"]["blocking"]), (["LNT-004/WARNING/REVIEW_REQUIRED"], True))
        sv = [("LNT-011", "INFO", "OPEN", "established", "src/a/rtl/dut.sv", 22, "m", "slang")]
        status = {r["code"]: r["status"] for r in self.rows(sv)[1]}
        self.assertEqual((status["WIDTHTRUNC"], status["UNUSEDSIGNAL"]), ("silent", "near"))

    def test_a_match_states_its_severity_and_another_rule_is_never_a_match(self):
        info = [("LNT-004", "INFO", "OPEN", "established", "src/a/rtl/dut.sv", 10, "m", "sverity"),
                ("LNT-082", "INFO", "OPEN", "established", "src/a/rtl/dut.sv", 20, "m", "sverity")]
        warnings, rows = self.rows(info)
        summary = [{"ip": "dut", "verilator": len(rows), "matched": 1, "matched_blocking": 0, "matched_info": 1, "near": 0, "other": 1,
                    "excluded": 1, "silent": 0}]
        doc = verilator_diff.document(summary, rows, [], {}, None)
        by = {x["code"]: x for x in doc["sites"]}
        self.assertEqual((by["WIDTHTRUNC"]["status"], by["WIDTHTRUNC"]["blocking"]), ("matched", False))
        self.assertEqual((by["UNUSEDSIGNAL"]["status"], by["UNUSEDSIGNAL"]["classification"]), ("other", verilator_diff.OTHER_RULE))
        self.assertEqual(doc["totals"]["matched"], 1)

    def test_the_sverity_only_direction_is_in_the_document(self):
        only = [{"ip": "dut", "kind": "blocking", "rule": "LNT-054", "file": "a.sv", "line": 3, "message": "m", "verilator_checks_class": False}]
        doc = verilator_diff.document([{"ip": "dut", "verilator": 0, "matched": 0, "matched_blocking": 0, "matched_info": 0, "near": 0,
                                        "other": 0, "excluded": 0, "silent": 0}], [], only, {}, None)
        self.assertEqual((doc["sverity_only"][0]["rule"], doc["sverity_only"][0]["locations"]), ("LNT-054", ["a.sv:3"]))


if __name__ == "__main__":
    unittest.main()
