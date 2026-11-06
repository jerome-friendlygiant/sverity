"""Exercise scorer gate failures using small synthetic cases, never the held-out corpus."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

BINARY = pathlib.Path(sys.argv.pop(1)).resolve()
SCORER = pathlib.Path(__file__).resolve().parents[1] / 'tools/dev/accuracy.py'


class AccuracyGate(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.suite = pathlib.Path(self.scratch.name)

    def case(self, verdict='good', family='CST', readers=2, invalid=False):
        case = self.suite / 'cases' / family / 'example'
        case.mkdir(parents=True)
        init = 'initial q=0; ' if invalid else ''
        (case / 'design.sv').write_text('module top(input logic clk,d, output logic q); '
                                      + init + 'always_ff @(posedge clk) q<=d; endmodule\n')
        label = dict(family=family, top='top', verdict=verdict, critical=True,
                     sites=[] if verdict == 'good' else [dict(line=1, signal='q')], author='author',
                     labels=[dict(by=str(i), verdict=verdict) for i in range(readers)])
        (case / 'label.json').write_text(json.dumps(label))
        return case, label

    def run_check(self, *options):
        return subprocess.run([sys.executable, str(SCORER), str(BINARY), '--suite', str(self.suite),
                               '--check', *options], capture_output=True, text=True)

    def test_missing_reading_cannot_pass(self):
        self.case(readers=1)
        result = self.run_check()
        self.assertEqual(result.returncode, 1)
        self.assertIn('labellers disagree', result.stderr)

    def test_failed_elaboration_cannot_be_clean(self):
        self.case(invalid=True)
        result = self.run_check()
        self.assertEqual(result.returncode, 1)
        self.assertIn('unexpected execution status ERROR', result.stderr)
        self.assertIn('execution error', result.stdout)

    def test_invalid_lint_defect_is_still_measured(self):
        self.case(verdict='bad', family='LNT', invalid=True)
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('detected', result.stdout)

    def test_adapted_case_requires_provenance(self):
        case, label = self.case()
        label['origin'] = 'adapted'
        (case / 'label.json').write_text(json.dumps(label))
        result = self.run_check()
        self.assertEqual(result.returncode, 1)
        self.assertIn('provenance', result.stderr)

    def test_empty_suite_cannot_pass(self):
        result = self.run_check()
        self.assertEqual(result.returncode, 1)
        self.assertIn('no cases', result.stderr)


if __name__ == '__main__':
    unittest.main()
