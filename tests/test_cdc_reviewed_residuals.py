"""Unit checks for the Step 4B reviewed-SDC residual measurement script."""
import importlib.util
import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("cdc_reviewed_residuals", ROOT / "tools/dev/cdc_reviewed_residuals.py")
residuals = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(residuals)


def _d(rule, severity="WARNING", confidence="established", count=1, **extra):
    return dict(rule_id=rule, severity=severity, confidence=confidence, count=count, **extra)


class ResidualTests(unittest.TestCase):
    def test_denominator_matches_manifest_designs(self):
        manifest = json.loads((ROOT / "tests/public_projects/manifest.json").read_text())
        self.assertEqual({x["name"] for x in manifest["known_good"]}, set(residuals.ANALYSED_LINES))
        self.assertEqual(sum(residuals.ANALYSED_LINES.values()), residuals.TOTAL_LINES)
        self.assertEqual(residuals.TOTAL_LINES, 2894)

    def test_reviewed_sdcs_are_public_and_path_free(self):
        for name in residuals.ANALYSED_LINES:
            text = (residuals.DEFAULT_SDC_DIR / f"{name}.sdc").read_text()
            self.assertTrue(text.startswith("# "), name)
            for needle in ("/" + "home/", "/" + "tmp/", "/" + "Users/"):
                self.assertNotIn(needle, text)
            commands = [x for x in text.splitlines() if x and not x.startswith("#")]
            self.assertTrue(any(x.startswith("create_clock") for x in commands), name)
            self.assertTrue(any(x.startswith("set_clock_groups -asynchronous") for x in commands), name)
            self.assertTrue(any(x.startswith("set_output_delay") for x in commands), name)

    def test_deck_rules_come_from_the_catalog(self):
        deck = residuals.deck_rules()
        self.assertEqual(len(deck), 60)
        self.assertIn("CDC-017", deck)
        self.assertNotIn("CDC-012", deck)

    def test_rdc_deck_rules_come_from_the_catalog(self):
        deck = residuals.deck_rules("rdc")
        self.assertEqual(len(deck), 40)
        self.assertIn("RDC-007", deck)
        self.assertNotIn("RDC-029", deck)
        self.assertEqual(residuals.DECKS["rdc"]["flags"][-2:], ["--deck", "rdc"])

    def test_tally_skips_info_and_separates_preview(self):
        deck = {"CDC-017", "CDC-024"}
        t = residuals.tally([
            _d("CDC-017", "ERROR"), _d("CDC-017", "INFO"),
            _d("CDC-024", confidence="heuristic"),            # generic obligation by convention
            _d("CDC-017", confidence="unresolved", count=3),
            _d("CDC-017", disposition="NOT_ANALYSED"),
            _d("LNT-005"), _d("LNT-005", "INFO"),
        ], deck)
        row = residuals._flat(t["deck"])
        self.assertEqual((row["non_info"], row["ERROR"], row["WARNING"], row["FATAL"]), (6, 1, 5, 0))
        self.assertEqual((row["established"], row["unresolved"], row["generic_obligation"], row["not_analysed"]), (1, 3, 1, 1))
        self.assertEqual(residuals._flat(t["preview"])["non_info"], 1)
        self.assertEqual(t["deck_by_rule"]["CDC-017"], 5)

    def test_budgets_use_unrounded_rates(self):
        row = {k: 0 for k in ("non_info", "FATAL", "ERROR", "WARNING", *residuals.CLASSES)}
        row.update(non_info=30, WARNING=30, unresolved=30)
        checks = residuals.budget_checks(row, 2.0)
        self.assertTrue(checks["obligations <= 15/kLOC"]["pass_"])          # exactly 15.0
        row.update(unresolved=31, non_info=31, WARNING=31)
        self.assertFalse(residuals.budget_checks(row, 2.0)["obligations <= 15/kLOC"]["pass_"])
        row.update(ERROR=1)
        self.assertFalse(residuals.budget_checks(row, 2.0)["ERROR == 0"]["pass_"])
        self.assertFalse(residuals.budget_checks(row, 2.0)["confirmed false positives <= 1/kLOC"]["machine_checked"])


if __name__ == "__main__":
    unittest.main()
