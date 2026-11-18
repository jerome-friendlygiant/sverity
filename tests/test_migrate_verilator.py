"""Migrating a Verilator policy keeps each directive's scope, and an SVerity waiver made from it really applies.

All inputs are synthetic, written to a temporary directory. Usage: test_migrate_verilator.py SVERITY_BINARY
"""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

BINARY = pathlib.Path(sys.argv.pop(1)).resolve()
MIGRATOR = pathlib.Path(__file__).resolve().parents[1] / 'tools/dev/migrate_verilator.py'

DESIGN_PATH = 'hw/dut/rtl/dut.sv'   # matched by the `*/rtl/dut.sv` globs below, as OpenTitan paths are
DESIGN = """module top(input logic clk, input logic d, output logic q);
  logic unused_sig;
  always_ff @(posedge clk) q <= d;
endmodule
"""


class Migration(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.dir = pathlib.Path(scratch.name)
        (self.dir / DESIGN_PATH).parent.mkdir(parents=True)
        (self.dir / DESIGN_PATH).write_text(DESIGN)

    def migrate(self, text, *options):
        vlt = self.dir / 'policy.vlt'
        vlt.write_text(text)
        waivers, report = self.dir / 'waivers.json', self.dir / 'report.md'
        result = subprocess.run([sys.executable, str(MIGRATOR), str(vlt), '--root', str(self.dir),
                                 '--waivers', str(waivers), '--report', str(report), *options],
                                capture_output=True, text=True)
        entries = json.loads(waivers.read_text())['waivers'] if waivers.exists() else []
        return result, entries, report.read_text() if report.exists() else ''

    def test_file_and_signal_pattern_keep_their_scope(self):
        result, waivers, _ = self.migrate(
            '`verilator_config\n'
            '// Clock only feeds assertions.\n'
            'lint_off -rule UNUSED -file "*/rtl/dut.sv" -match "Signal is not used: \'clk_i\'"\n',
            '--owner', 'test')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([(w['rule'], w['file'], w['hierarchy']) for w in waivers],
                         [('LNT-011', '*/rtl/dut.sv', '*.clk_i')])
        self.assertEqual(waivers[0]['owner'], 'test')
        self.assertTrue(waivers[0]['reason'].endswith(': Clock only feeds assertions.'))

    def test_message_without_a_signal_is_not_migrated(self):
        result, waivers, report = self.migrate(
            'lint_off -rule UNUSED -file "*/rtl/dut.sv" -match "*Parameter is not used*"\n')
        self.assertEqual(waivers, [])
        self.assertIn('names no quoted signal', report)

    def test_message_text_beyond_the_signal_is_not_dropped(self):
        # A WIDTH message names an operator and widths; keeping only the signal would widen the waiver.
        result, waivers, report = self.migrate(
            'lint_off -rule WIDTH -file "*/rtl/dut.sv" -match "Operator EQ expects 32 bits on the LHS, but '
            'LHS\'s VARREF \'unused_sig\' generates 2 bits."\n')
        self.assertEqual(waivers, [])
        self.assertIn('would be dropped', report)

    def test_global_lint_off_is_a_policy_not_a_waiver(self):
        result, waivers, report = self.migrate('lint_off -rule PINCONNECTEMPTY\n')
        self.assertEqual(waivers, [])
        self.assertIn('## Global policies', report)
        self.assertIn('LNT-010', report)

    def test_global_policy_with_an_exact_option_goes_to_the_config(self):
        config = self.dir / 'config.json'
        _, entries, report = self.migrate('`verilator_config\nlint_off -rule PINCONNECTEMPTY\nlint_off -rule WIDTH\n',
                                          '--config', str(config))
        self.assertEqual(entries, [])
        self.assertEqual(json.loads(config.read_text()), {'options': {'LNT-010.empty_output': 'info'}})
        self.assertIn('`LNT-010.empty_output=info`', report)   # WIDTH has no exact option: listed, not expressed
        result = subprocess.run([str(BINARY), '--config', str(config), '--top', 'top', '--goal', 'lint', '--json', '-',
                                 str(self.dir / DESIGN_PATH)], capture_output=True, text=True)
        self.assertIn(result.returncode, (0, 4, 6), result.stderr)   # the configuration is accepted and applied
        self.assertEqual(json.loads(result.stdout)['inputs']['rule_options']['LNT-010.empty_output'], 'info')

    def test_line_range_gives_one_waiver_per_line_and_long_ranges_stay_out(self):
        result, waivers, report = self.migrate(
            'lint_off -rule BLKSEQ -file "*/rtl/dut.sv" -lines 3-5\n'
            'lint_off -rule BLKSEQ -file "*/rtl/dut.sv" -lines 1-60\n')
        self.assertEqual(sorted(w['line'] for w in waivers), [3, 4, 5])
        self.assertIn('line range of 60 lines', report)

    def test_unknown_code_is_listed_not_migrated(self):
        result, waivers, report = self.migrate('lint_off -rule NOSUCHCODE -file "*/rtl/dut.sv"\n')
        self.assertEqual(waivers, [])
        self.assertIn('code not in the migration table', report)

    def test_lint_on_cancels_the_same_scope_and_flags_another(self):
        result, waivers, report = self.migrate(
            'lint_off -rule LATCH -file "*/rtl/dut.sv"\n'
            'lint_on -rule LATCH -file "*/rtl/dut.sv"\n'
            'lint_off -rule COMBDLY -file "*/rtl/dut.sv"\n'
            'lint_on -rule COMBDLY -file "*/rtl/dut.sv" -lines 9\n')
        self.assertEqual(waivers, [])
        self.assertIn('cancelled by lint_on at line 2', report)
        self.assertIn('has another scope; not modelled', report)

    def test_reason_without_a_comment_names_the_code(self):
        result, waivers, _ = self.migrate('\nlint_off -rule BLKSEQ -file "*/rtl/dut.sv"\n')
        self.assertEqual(waivers[0]['reason'], 'migrated from policy.vlt:2: Verilator lint_off BLKSEQ')

    def test_other_directives_are_counted_not_migrated(self):
        result, waivers, report = self.migrate('split_var -module "top" -var "q"\n')
        self.assertEqual(waivers, [])
        self.assertIn('| split_var | 1 |', report)

    def test_unreadable_input_exits_2(self):
        result = subprocess.run([sys.executable, str(MIGRATOR), str(self.dir / 'missing.vlt'), '--root', str(self.dir),
                                 '--waivers', str(self.dir / 'w.json'), '--report', str(self.dir / 'r.md')],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)

    def test_migrated_waiver_applies_to_the_slang_unused_variable(self):
        # Slang's unused-variable finding carries its occurrence path, not `signals`, so the waiver narrows by hierarchy.
        _, waivers, _ = self.migrate('lint_off -rule UNUSED -file "*/rtl/dut.sv" -match "Signal is not used: \'unused_sig\'"\n',
                                     '--owner', 'test')
        waiver_file = self.dir / 'waivers.json'
        base = [str(BINARY), '--top', 'top', '--root', str(self.dir), '--json', '-', DESIGN_PATH]

        def lnt011(*extra):
            run = subprocess.run(base[:-1] + list(extra) + base[-1:], cwd=self.dir, capture_output=True, text=True)
            report = json.loads(run.stdout)
            self.assertFalse([d for d in report['diagnostics'] if d['rule_id'] == 'CST-006'], 'a waiver was rejected')
            return [d['disposition'] for d in report['diagnostics'] if d['rule_id'] == 'LNT-011']

        self.assertEqual(lnt011(), ['OPEN'])  # unused declarations are advisory; waiver scope is unchanged
        self.assertEqual(lnt011('--waivers', str(waiver_file)), ['WAIVED'])


if __name__ == '__main__':
    unittest.main()
