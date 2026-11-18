"""Exercise scorer gate failures using small synthetic cases, never the held-out corpus."""
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

BINARY = pathlib.Path(sys.argv.pop(1)).resolve()
SCORER = pathlib.Path(__file__).resolve().parents[1] / 'tools/dev/accuracy.py'


def digest(label):
    """The `readings_sha256` a record pins: the readings and settlement as the case stores them (the scorer's own definition)."""
    import hashlib
    text = json.dumps({'labels': label.get('labels', []), 'settlement': label.get('settlement'), 'top': label.get('top'),
                       'author': label.get('author')}, sort_keys=True, separators=(',', ':'))
    return hashlib.sha256(text.encode()).hexdigest()


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

    def test_lc01_syntax_rejection_is_detected(self):
        case, label = self.case(verdict='bad', family='LNT')
        (case / 'design.sv').write_text('module top(output logic q); assign q = ; endmodule\n')
        label['class'] = 'LC01'
        (case / 'label.json').write_text(json.dumps(label))
        output = self.suite / 'score.json'
        result = self.run_check('--json', str(output))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        row = json.loads(output.read_text())['cases'][0]
        self.assertEqual(row['outcome'], 'detected')
        self.assertTrue(row['expected_rejection'])

    def test_lc01_unknown_case_rejected_by_the_frontend_is_disclosed(self):
        # an unknown module leaves the question of the design open: the frontend's rejection at the labelled line discloses it
        case, label = self.case(verdict='unknown', family='LNT')
        (case / 'design.sv').write_text('module top(output logic q); missing_cell u(.q(q)); endmodule\n')
        label['class'] = 'LC01'
        (case / 'label.json').write_text(json.dumps(label))
        output = self.suite / 'score.json'
        result = self.run_check('--json', str(output))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        row = json.loads(output.read_text())['cases'][0]
        self.assertEqual(row['outcome'], 'disclosed')
        self.assertTrue(row['expected_rejection'])

    def test_lc07_frontend_failure_is_not_detection(self):
        case, label = self.case(verdict='bad', family='LNT', invalid=True)
        label['class'] = 'LC07'
        (case / 'label.json').write_text(json.dumps(label))
        output = self.suite / 'score.json'
        result = self.run_check('--json', str(output))
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        row = json.loads(output.read_text())['cases'][0]
        self.assertEqual(row['outcome'], 'execution error')
        self.assertTrue(any(d['rule_id'] == 'LNT-009' for d in row['diagnostics']))
        self.assertFalse(row.get('expected_rejection', False))

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


class SyntheticAccounting(unittest.TestCase):
    def test_suite_continues_and_accounts_for_failures(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            binary = root / 'fake'
            binary.write_text("""#!/usr/bin/env python3
import json, pathlib, signal, sys, time
if '--version' in sys.argv:
 print('synthetic'); sys.exit(0)
name = pathlib.Path(sys.argv[-1]).parent.name
if name == 'timeout': time.sleep(10)
if name == 'crash': signal.raise_signal(signal.SIGKILL)
if name == 'missing': sys.exit(8)
out = pathlib.Path(sys.argv[sys.argv.index('--json') + 1])
if name == 'malformed': out.write_text('{'); sys.exit(0)
if name == 'schema': out.write_text('{}'); sys.exit(0)
invalid_fields = {
 'location_line': {'location': {'file': 'design.sv', 'line': 'oops'}},
 'location_file': {'location': {'file': 42, 'line': 1}},
 'evidence': {'evidence': [42]},
 'evidence_location': {'evidence': [{'location': {'file': 'design.sv', 'line': []}}]},
 'signals': {'signals': [42]},
 'message': {'message': []},
 'confidence': {'confidence': []},
}

error = name in ('internal', 'expected')
diag = []
if name in invalid_fields: diag = [dict(rule_id='LNT-009', severity='ERROR', **invalid_fields[name])]
if name == 'internal': diag = [{'rule_id':'LNT-001','severity':'FATAL','message':'internal failure while loading the design'}]
if name == 'expected': diag = [{'rule_id':'LNT-001','severity':'FATAL','message':'failed'}, {'rule_id':'LNT-001','severity':'ERROR','origin':'slang','location':{'file':'design.sv','line':1}}]
out.write_text(json.dumps({'schema':'sverity-report/1','summary':{'execution_status':'ERROR' if error else 'SUCCESS','analysis_coverage':'INCOMPLETE'},'diagnostics':diag}))
sys.exit(8 if error or name == 'inconsistent' else 4)
""")
            binary.chmod(0o755)
            failures = ('timeout', 'crash', 'missing', 'malformed', 'schema', 'inconsistent', 'internal',
                        'location_line', 'location_file', 'evidence', 'evidence_location', 'signals', 'message', 'confidence')
            for name in failures + ('expected', 'valid'):
                case = root / 'cases' / 'LNT' / name
                case.mkdir(parents=True)
                (case / 'design.sv').write_text('module top; endmodule\n')
                verdict = 'good' if name == 'valid' else 'bad'
                label = dict(family='LNT', top='top', verdict=verdict, critical=True,
                             sites=[] if verdict == 'good' else [dict(line=1)], author='a',
                             labels=[dict(by='a', verdict=verdict), dict(by='b', verdict=verdict)],
                             **{'class': 'LC01' if name == 'expected' else 'LC02'})
                (case / 'label.json').write_text(json.dumps(label))
            # Import the scorer and shorten only the synthetic command timeout.
            import importlib.util
            from unittest import mock
            spec = importlib.util.spec_from_file_location('accuracy', SCORER)
            scorer = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(scorer)
            real_run = subprocess.run
            def short_run(*args, **kwargs):
                if kwargs.get('timeout') == 300:
                    kwargs['timeout'] = 0.2
                return real_run(*args, **kwargs)
            output = root / 'score.json'
            with mock.patch.object(scorer.subprocess, 'run', side_effect=short_run), mock.patch.object(
                    sys, 'argv', ['accuracy', str(binary), '--suite', str(root), '--check', '--json', str(output)]):
                self.assertEqual(scorer.main(), 1)
            data = json.loads(output.read_text())
            rows = {r['case'].split('/')[-1]: r for r in data['cases']}
            self.assertEqual(len(rows), len(failures) + 2)
            for name in failures:
                self.assertEqual(rows[name]['outcome'], 'execution error', name)
                self.assertTrue(rows[name]['reason'], name)
            self.assertTrue(rows['timeout']['timeout'])
            self.assertEqual(rows['expected']['outcome'], 'detected')
            self.assertTrue(rows['expected']['expected_rejection'])
            self.assertEqual(rows['valid']['outcome'], 'clean')
            self.assertEqual(rows['expected']['diagnostics'][1]['origin'], 'slang')
            self.assertIn('LC03', data['families']['LNT']['critical_classes_missing_bad'])
            self.assertGreater(data['families']['LNT']['critical_classes_undetected'], 0)


class FreezeProvenance(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.case = self.root / 'cases/LNT/example'
        self.case.mkdir(parents=True)
        self.label = dict(family='LNT', top='top', verdict='good', sites=[], author='author',
                          labels=[dict(by='one', verdict='good'), dict(by='two', verdict='good')],
                          batch='fresh', **{'class': 'LC02'})
        (self.case / 'label.json').write_text(json.dumps(self.label))
        (self.case / 'design.sv').write_text('module top; endmodule\n')
        self.marker = self.root / 'launched'
        self.binary = self.root / 'fake'
        self.binary.write_text("""#!/usr/bin/env python3
import json, pathlib, sys
pathlib.Path(__file__).with_name('launched').write_text('yes')
if '--version' in sys.argv: print('fake 1'); sys.exit(0)
p = pathlib.Path(sys.argv[sys.argv.index('--json')+1])
p.write_text(json.dumps({'schema':'sverity-report/1','summary':{'execution_status':'SUCCESS','analysis_coverage':'COMPLETE'},'diagnostics':[]}))
""")
        self.binary.chmod(0o755)
        self.manifest = self.root / 'freeze.json'
        result = self.invoke('--batch', 'fresh', '--freeze-batch', str(self.manifest))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.marker.exists())

    def invoke(self, *args):
        return subprocess.run([sys.executable, str(SCORER), '--suite', str(self.root), *args],
                              capture_output=True, text=True)

    def verified(self, output):
        return self.invoke(str(self.binary), '--batch', 'fresh', '--verify-freeze', str(self.manifest),
                           '--first-score', '--json', str(output))

    def test_exact_freeze_and_exclusive_first_score(self):
        output = self.root / 'result.json'
        result = self.verified(output)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(output.read_text())
        self.assertEqual(data['cases'][0]['outcome'], 'clean')
        self.assertEqual(data['provenance']['binary_version'], 'fake 1')
        self.assertEqual(data['provenance']['selected_inputs'], json.loads(self.manifest.read_text()))
        original = output.read_bytes()
        self.marker.unlink()
        self.assertNotEqual(self.verified(output).returncode, 0)
        self.assertFalse(self.marker.exists())
        self.assertEqual(output.read_bytes(), original)

    def test_mutations_rejected_before_launch(self):
        import shutil
        original_label = (self.case / 'label.json').read_bytes()
        original_design = (self.case / 'design.sv').read_bytes()
        for kind in ('design', 'label', 'add_file', 'add_case', 'delete_case'):
            with self.subTest(kind=kind):
                if kind == 'design': (self.case / 'design.sv').write_text('changed')
                if kind == 'label': (self.case / 'label.json').write_bytes(original_label + b' ')
                if kind == 'add_file': (self.case / 'extra.svh').write_text('extra')
                if kind == 'add_case': shutil.copytree(self.case, self.case.parent / 'another')
                if kind == 'delete_case': (self.case / 'label.json').unlink()
                result = self.verified(self.root / 'result.json')
                self.assertNotEqual(result.returncode, 0, kind)
                self.assertFalse(self.marker.exists(), kind)
                self.assertFalse((self.root / 'result.json').exists(), kind)
                (self.case / 'label.json').write_bytes(original_label)
                (self.case / 'design.sv').write_bytes(original_design)
                (self.case / 'extra.svh').unlink(missing_ok=True)
                shutil.rmtree(self.case.parent / 'another', ignore_errors=True)

    def test_symlinked_case_root_rejected_before_launch(self):
        external = self.root / 'external-case'
        self.case.rename(external)
        self.case.symlink_to(external, target_is_directory=True)
        result = self.verified(self.root / 'result.json')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('symlinked frozen case roots', result.stderr)
        self.assertFalse(self.marker.exists())
        self.assertFalse((self.root / 'result.json').exists())

    def test_labels_only_collects_all_problems_without_inventory(self):
        import shutil
        other = self.case.parent / 'other'
        shutil.copytree(self.case, other)
        label = dict(self.label, labels=[dict(by='one', verdict='good')])
        (other / 'label.json').write_text(json.dumps(label))
        (self.case / 'design.sv').unlink()
        result = self.invoke('--labels-only', '--batch', 'fresh')
        self.assertEqual(result.returncode, 1)
        self.assertIn('no design.sv', result.stderr)
        self.assertIn('labellers disagree', result.stderr)
        self.assertNotIn('Traceback', result.stderr)
        self.assertFalse(self.marker.exists())

    def test_regression_accounts_for_missing_source_and_continues(self):
        import shutil
        shutil.copytree(self.case, self.case.parent / 'other')
        (self.case / 'design.sv').unlink()
        output = self.root / 'regression.json'
        result = self.invoke(str(self.binary), '--batch', 'fresh', '--check', '--json', str(output))
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        data = json.loads(output.read_text())
        rows = {row['case']: row for row in data['cases']}
        self.assertEqual(rows['LNT/example']['execution'], 'NOT RUN')
        self.assertEqual(rows['LNT/example']['outcome'], 'execution error')
        self.assertIn('cannot read design.sv', rows['LNT/example']['reason'])
        self.assertEqual(rows['LNT/other']['outcome'], 'clean')
        self.assertIn('error', data['provenance']['selected_inputs'])

    def test_regression_and_context_hashes(self):
        import hashlib
        budget = self.root / 'budget.json'
        budget.write_text('{}')
        output = self.root / 'regression.json'
        result = self.invoke(str(self.binary), '--batch', 'fresh', '--budgets', str(budget), '--json', str(output))
        self.assertEqual(result.returncode, 0, result.stderr)
        p = json.loads(output.read_text())['provenance']
        self.assertEqual(p['binary_sha256'], hashlib.sha256(self.binary.read_bytes()).hexdigest())
        self.assertEqual(p['budgets_sha256'], hashlib.sha256(budget.read_bytes()).hexdigest())
        self.assertEqual(p['catalog_sha256'], hashlib.sha256((SCORER.parents[2] / 'docs/rules/catalog.json').read_bytes()).hexdigest())
        old_binary, old_budget = p['binary_sha256'], p['budgets_sha256']
        self.binary.write_text(self.binary.read_text() + '\n# changed\n')
        budget.write_text('{"families":{}}')
        result = self.invoke(str(self.binary), '--batch', 'fresh', '--budgets', str(budget), '--json', str(output))
        self.assertEqual(result.returncode, 0, result.stderr)
        p = json.loads(output.read_text())['provenance']
        self.assertNotEqual(p['binary_sha256'], old_binary)
        self.assertNotEqual(p['budgets_sha256'], old_budget)

    def test_catalog_hash_tracks_context(self):
        import importlib.util
        from unittest import mock
        spec = importlib.util.spec_from_file_location('accuracy_context', SCORER)
        scorer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(scorer)
        docs = self.root / 'docs/rules'
        docs.mkdir(parents=True)
        (docs / 'catalog.json').write_text('one')
        (docs / 'LINT_WORKFLOW.md').write_text('workflow')
        class Args:
            binary = self.binary
            budgets = None
            verify_freeze = None
            batch = 'fresh'
            check = False
            first_score = False
        with mock.patch.object(scorer, 'REPO', self.root), mock.patch.object(scorer, 'git_provenance', return_value={}):
            first = scorer.measurement_provenance(Args, {}, 'fake')
            (docs / 'catalog.json').write_text('two')
            second = scorer.measurement_provenance(Args, {}, 'fake')
        self.assertNotEqual(first['catalog_sha256'], second['catalog_sha256'])
        self.assertEqual(first['workflow_sha256'], second['workflow_sha256'])


class InputPreflight(FreezeProvenance):
    def test_malformed_label_then_valid_case_continues(self):
        import shutil
        broken = self.case.parent / 'aaa'
        shutil.copytree(self.case, broken)
        (broken / 'label.json').write_text('{')
        output = self.root / 'result.json'
        result = self.invoke(str(self.binary), '--batch', 'fresh', '--check', '--json', str(output))
        self.assertNotEqual(result.returncode, 0)
        rows = json.loads(output.read_text())['cases']
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]['batch_membership'], 'unknown')
        self.assertEqual(rows[0]['execution'], 'NOT RUN')
        self.assertIn('cannot read label', rows[0]['reason'])
        self.assertEqual(rows[1]['outcome'], 'clean')

    def test_nested_errors_and_labels_only_aggregate(self):
        import shutil
        bad = self.case.parent / 'aaa'
        shutil.copytree(self.case, bad)
        malformed = dict(self.label, sites=[{'line': 'one'}], labels=[{'verdict': 'good', 'sites': [None]}], sdc=[{}])
        (bad / 'label.json').write_text(json.dumps(malformed))
        (self.case / 'label.json').write_text('{')
        result = self.invoke('--labels-only', '--batch', 'fresh')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('line must be a positive integer', result.stderr)
        self.assertIn('entries must be objects', result.stderr)
        self.assertIn('sdc must be a list', result.stderr)
        self.assertIn('cannot read label', result.stderr)
        self.assertFalse(self.marker.exists())

    def test_bad_budget_before_version_and_reservation(self):
        budget = self.root / 'budget.json'
        output = self.root / 'first.json'
        for content in ('{', '{"families": []}', '{"families":{"LNT":{"max_missed_rate":"bad"}}}',
                        json.dumps({'families': {'LNT': {'max_missed_rate': 10 ** 400}}})):
            budget.write_text(content)
            result = self.invoke(str(self.binary), '--batch', 'fresh', '--verify-freeze', str(self.manifest),
                                 '--first-score', '--json', str(output), '--budgets', str(budget))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('budget error', result.stderr)
            self.assertFalse(self.marker.exists())
            self.assertFalse(output.exists())

    def test_legacy_empty_settled_by_is_no_settlement(self):
        label = dict(self.label, settled_by='')
        (self.case / 'label.json').write_text(json.dumps(label))
        result = self.invoke('--labels-only', '--batch', 'fresh')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.marker.exists())

    def test_legacy_structured_third_reading_is_preserved(self):
        label = dict(self.label, settled_by=dict(by='third', verdict='good', reason='independent adjudication'))
        label['labels'] = [dict(by='one', verdict='good'), dict(by='two', verdict='bad')]
        (self.case / 'label.json').write_text(json.dumps(label))
        result = self.invoke('--labels-only', '--batch', 'fresh')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.marker.exists())

    def test_settlement_types_checked_before_equality(self):
        evidence = dict(verdict='good', **{'class': 'LC02'}, sites=[dict(line=1)], critical=True)
        label = dict(self.label, **evidence,
                     labels=[dict(evidence, by='one'), dict(evidence, by='two', critical=False)])
        for changed in ({'critical': 1}, {'sites': [{'line': True}]}):
            with self.subTest(changed=changed):
                label['settlement'] = {**evidence, 'by': 'third', **changed}
                (self.case / 'label.json').write_text(json.dumps(label))
                result = self.invoke('--labels-only', '--batch', 'fresh', '--strict-fresh-labels')
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('strict fresh settlement', result.stderr)
        label['settlement'] = dict(evidence, by='third')
        (self.case / 'label.json').write_text(json.dumps(label))
        result = self.invoke('--labels-only', '--batch', 'fresh', '--strict-fresh-labels')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.marker.exists())

    def test_strict_declared_readings_and_legacy(self):
        output = self.root / 'output.json'
        # Existing labels remain usable in ordinary regression, but omit fresh evidence.
        self.assertEqual(self.invoke(str(self.binary), '--batch', 'fresh', '--json', str(output)).returncode, 0)
        self.marker.unlink()
        def check(label):
            (self.case / 'label.json').write_text(json.dumps(label))
            return self.invoke('--labels-only', '--batch', 'fresh', '--strict-fresh-labels')
        base = dict(self.label, critical=False)
        reading = dict(verdict='good', **{'class': 'LC02'}, sites=[], critical=False)
        for ids in (('reader', 'reader'), ('author', 'reader')):
            label = dict(base, labels=[dict(reading, by=ids[0]), dict(reading, by=ids[1])])
            self.assertNotEqual(check(label).returncode, 0)
        label = dict(base, labels=[dict(reading, by='reader1'), dict(reading, by='reader2')])
        self.assertEqual(check(label).returncode, 0)
        disagreement = dict(label, labels=[dict(reading, by='reader1'), dict(reading, by='reader2', critical=True)])
        self.assertNotEqual(check(disagreement).returncode, 0)
        disagreement['settlement'] = dict(reading, by='reader3')
        self.assertEqual(check(disagreement).returncode, 0)
        disagreement['settlement']['by'] = 'reader1'
        self.assertNotEqual(check(disagreement).returncode, 0)
        self.assertFalse(self.marker.exists())


class Adjudication(unittest.TestCase):
    """A post-score adjudication that names its record supersedes the readings and the settlement."""
    WAS = dict(verdict='bad', critical=True, **{'class': 'LC02'})
    NOW = dict(verdict='good', critical=False, sites=[], **{'class': 'LC02'})

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = pathlib.Path(self.tmp.name)
        self.suite = self.repo / 'tests/accuracy'
        self.case = self.suite / 'cases/LNT/example'
        self.case.mkdir(parents=True)
        (self.case / 'design.sv').write_text('module top; endmodule\n')
        (self.suite / 'results').mkdir()
        self.record = 'tests/accuracy/results/adjudication.json'
        reading = dict(verdict='bad', sites=[dict(line=1)], critical=True, **{'class': 'LC02'})
        self.label = dict(family='LNT', top='top', batch='fresh', author='author', verdict='good', sites=[],
                          critical=False, labels=[dict(reading, by='one (model-a, blind)'), dict(reading, by='two (model-b, blind)')],
                          adjudication=dict(by='maintainer delegate, policy decision', was=dict(self.WAS), reason='narrowed', record=self.record), **{'class': 'LC02'})
        self.write_record()

    def write_record(self, batch='fresh', was=None, now=None, case='LNT/example', extra=(), label=None):
        entry = {'case': case, 'was': was or dict(self.WAS), 'now': now or dict(self.NOW), 'reason': 'narrowed',
                 'decided_by': 'delegate', 'readings_sha256': digest(label or self.label)}
        (self.repo / self.record).write_text(json.dumps({'batch': batch, 'cases': [entry, *extra]}))

    def check(self, label):
        (self.case / 'label.json').write_text(json.dumps(label))
        return subprocess.run([sys.executable, str(SCORER), '--suite', str(self.suite), '--labels-only',
                               '--batch', 'fresh', '--strict-fresh-labels'], capture_output=True, text=True)

    def rejected(self, label, text):
        result = self.check(label)
        self.assertNotEqual(result.returncode, 0, text)
        self.assertIn(text, result.stderr)

    def test_adjudicated_label_is_neither_a_dispute_nor_a_mismatch(self):
        result = self.check(self.label)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_without_adjudication_the_label_must_match_the_readings(self):
        label = {k: v for k, v in self.label.items() if k != 'adjudication'}
        self.rejected(label, 'independent third settlement')

    def test_record_must_exist_and_list_the_case(self):
        for record, text in (('tests/accuracy/results/missing.json', 'cannot read adjudication record'),
                             ('../outside.json', 'relative repo path')):
            self.rejected(dict(self.label, adjudication=dict(self.label['adjudication'], record=record)), text)
        self.write_record(case='LNT/other')
        self.rejected(self.label, 'does not list this case')
        self.write_record(case='example')   # not keyed by family
        self.rejected(self.label, 'does not list this case')

    def test_duplicate_record_entries_are_an_error(self):
        self.write_record(extra=[{'case': 'LNT/example', 'was': dict(self.WAS), 'now': dict(self.NOW)}])
        self.rejected(self.label, '2 times')

    def test_record_must_agree_with_the_label(self):
        self.write_record(batch='other')
        self.rejected(self.label, "is for batch 'other'")
        self.write_record(was=dict(self.WAS, verdict='good'))
        self.rejected(self.label, 'says the case was')
        self.write_record(now=dict(self.NOW, verdict='bad'))
        self.rejected(self.label, 'says the case is now')

    def test_final_class_critical_and_sites_are_the_records_not_the_labels(self):
        for field, value in (('critical', True), ('class', 'LC17'), ('sites', [dict(line=3)])):
            self.write_record(now=dict(self.NOW, **{field: value}))
            self.rejected(self.label, 'says the case is now')
        # a label changed behind a record that still says the old thing
        self.write_record()
        self.rejected(dict(self.label, critical=True), 'says the case is now')
        self.rejected(dict(self.label, **{'class': 'LC17'}), 'says the case is now')

    def test_adjudication_must_change_the_reading(self):
        self.write_record(now=dict(self.WAS, sites=[dict(line=1)]))
        self.rejected(dict(self.label, verdict='bad', critical=True, sites=[dict(line=1)]), 'to the same reading')

    def test_adjudication_moves_from_what_the_readings_settled(self):
        label = dict(self.label, adjudication=dict(self.label['adjudication'], was=dict(self.WAS, verdict='unknown')))
        self.write_record(was=dict(self.WAS, verdict='unknown'))
        self.rejected(label, 'the readings settled on')
        for field, value in (('critical', False), ('class', 'LC17')):   # the whole reading is compared, not the verdict alone
            was = dict(self.WAS, **{field: value})
            self.write_record(was=was)
            self.rejected(dict(self.label, adjudication=dict(self.label['adjudication'], was=was)), 'the readings settled on')

    def test_a_settlement_must_name_who_settled_it(self):
        settlement = dict(verdict='bad', sites=[dict(line=1)], critical=True, **{'class': 'LC02'})
        self.rejected(dict(self.label, settlement=settlement), 'names who settled it')
        settled = dict(self.label, settlement=dict(settlement, by='third'))
        self.write_record(label=settled)
        result = self.check(settled)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_a_reader_cannot_settle_the_case(self):
        settled = dict(self.label, settlement=dict(verdict='bad', sites=[dict(line=1)], critical=True, by='One (model-c)', **{'class': 'LC02'}))
        self.write_record(label=settled)
        self.rejected(settled, 'names who settled it')

    def test_editing_a_reader_or_the_settlement_behind_the_record_is_caught(self):
        widened = dict(self.label, labels=[dict(self.label['labels'][0], sites=[dict(line=1), dict(line=9)]), self.label['labels'][1]])
        self.rejected(widened, 'readings_sha256 does not match')
        other_class = dict(self.label, labels=[dict(r, **{'class': 'LC03'}) for r in self.label['labels']])
        self.rejected(other_class, 'readings_sha256 does not match')
        settlement = dict(verdict='bad', sites=[dict(line=1)], critical=True, by='third', **{'class': 'LC02'})
        self.write_record(label=dict(self.label, settlement=settlement))
        self.rejected(dict(self.label, settlement=dict(settlement, sites=[dict(line=1), dict(line=5)])), 'readings_sha256 does not match')

    def test_top_and_author_are_pinned_by_the_record(self):
        self.rejected(dict(self.label, top='other_top'), 'readings_sha256 does not match')
        self.rejected(dict(self.label, author='someone else'), 'readings_sha256 does not match')

    def test_settler_ids_match_across_space_underscore_and_hyphen(self):
        readers = [dict(self.label['labels'][0], by='Reader-1 (model-a, blind)'), dict(self.label['labels'][1], by='Reader-2 (model-b, blind)')]
        base = dict(self.label, labels=readers)
        self.write_record(label=base)
        self.assertEqual(self.check(base).returncode, 0)
        for by in ('Reader 1, third reading', 'reader_1', 'Reader-1, third reading'):   # the settler
            settled = dict(base, settlement=dict(verdict='bad', sites=[dict(line=1)], critical=True, by=by, **{'class': 'LC02'}))
            self.write_record(label=settled)
            self.rejected(settled, 'names who settled it')

    def test_an_adjudicated_case_scores_only_the_records_sites(self):
        """A reader's extra site never widens what counts as detected once the case is adjudicated."""
        spec = importlib.util.spec_from_file_location('accuracy_under_test', SCORER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        widened = dict(self.label, labels=[dict(self.label['labels'][0], sites=[dict(line=1), dict(line=9)]), self.label['labels'][1]])
        self.assertEqual(module.sites_of(widened), widened['sites'])
        unadjudicated = {k: v for k, v in widened.items() if k != 'adjudication'}
        self.assertIn(dict(line=9), module.sites_of(unadjudicated))

    def test_adjudicator_is_not_the_author_a_reader_or_the_settler(self):
        for by in ('author', 'one', 'two', 'Author', 'ONE (another-model)', 'two (model-c, blind)', 'One, after the score', 'delegate and Author'):
            label = dict(self.label, adjudication=dict(self.label['adjudication'], by=by))
            self.rejected(label, 'must not be the author, a reader or the settler')

    def test_adjudicator_names_are_compared_as_whole_ids_without_separators(self):
        reading = dict(verdict='bad', sites=[dict(line=1)], critical=True, **{'class': 'LC02'})
        label = dict(self.label, labels=[dict(reading, by='reader-1 (model-a, blind)'), dict(reading, by='reader-2, blind')])
        self.write_record(label=label)
        for by in ('Reader1', 'Reader\u20131', 'reader_1', 'READER 1 (third reading)', 'reader 2 (third reading)', 'Reader2', 'delegate and Reader\u20132'):
            self.rejected(dict(label, adjudication=dict(self.label['adjudication'], by=by)), 'must not be the author, a reader or the settler')
        for by in ('Reader 12, delegate', 'reader-10 (model), delegate', 'reader-21 delegate'):
            result = self.check(dict(label, adjudication=dict(self.label['adjudication'], by=by)))
            self.assertEqual(result.returncode, 0, by + result.stderr)

    def test_record_is_not_a_label(self):
        record = 'tests/accuracy/cases/LNT/example/label.json'
        self.rejected(dict(self.label, adjudication=dict(self.label['adjudication'], record=record)), 'is a label, not a record')

    def test_score_counts_adjudicated_labels(self):
        (self.case / 'label.json').write_text(json.dumps(self.label))
        result = subprocess.run([sys.executable, str(SCORER), str(BINARY), '--suite', str(self.suite), '--batch', 'fresh'],
                                capture_output=True, text=True)
        self.assertIn('adjudicated labels (scored at their adjudicated verdict): LNT 1', result.stdout, result.stdout + result.stderr)

    def test_adjudication_by_and_reason_follow_the_record(self):
        self.rejected(dict(self.label, adjudication=dict(self.label['adjudication'], by='another nonparticipant')), "does not name the record's decided_by")
        self.rejected(dict(self.label, adjudication=dict(self.label['adjudication'], reason='a different reason')), 'differs from the reason the record states')
        self.assertEqual(self.check(dict(self.label, adjudication=dict(self.label['adjudication'], by='Delegate (model)'))).returncode, 0)

    def test_adjudication_needs_its_fields(self):
        self.rejected(dict(self.label, adjudication=dict(by='delegate', record=self.record)), 'adjudication requires by, reason and record')
        self.rejected(dict(self.label, adjudication=dict(self.label['adjudication'], was=dict(verdict='bad'))), 'adjudication.was requires')


class ClassContracts(unittest.TestCase):
    """A critical class the workflow says is disclosed, not decided (budgets.json `class_contracts`) is covered by disclosure: its bad and unknown cases are all disclosed."""

    @staticmethod
    def scorer():
        import importlib.util
        spec = importlib.util.spec_from_file_location('accuracy_contracts', SCORER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    @staticmethod
    def rows(scorer, lc24):
        """Every critical class detected once, and LC24 with the given (verdict, outcome) of its cases (an outcome alone is a bad case)."""
        rows = [dict(family='LNT', disputed=False, verdict='bad', outcome='detected', **{'class': k})
                for k, critical in scorer.CLASS_CRITICAL.items() if critical and k != 'LC24']
        rows += [dict(family='LNT', disputed=False, verdict=o[0] if isinstance(o, tuple) else 'bad', outcome=o[1] if isinstance(o, tuple) else o, **{'class': 'LC24'}) for o in lc24]
        return rows

    def test_disclosed_cases_cover_a_disclosure_class(self):
        scorer = self.scorer()
        contracts = {'LC24': 'disclosure'}
        self.assertEqual(scorer.undetected_critical_classes(self.rows(scorer, ['unresolved', 'unresolved']), None), 1)
        self.assertEqual(scorer.undetected_critical_classes(self.rows(scorer, ['unresolved', 'detected']), contracts), 0)
        self.assertEqual(scorer.undetected_critical_classes(self.rows(scorer, ['unresolved']), contracts), 0)
        self.assertEqual(scorer.undetected_critical_classes(self.rows(scorer, [('good', 'clean'), ('unknown', 'disclosed')]), contracts), 0)

    def test_a_missed_or_absent_case_leaves_it_uncovered(self):
        scorer = self.scorer()
        contracts = {'LC24': 'disclosure'}
        self.assertEqual(scorer.undetected_critical_classes(self.rows(scorer, ['unresolved', 'missed']), contracts), 1)
        self.assertEqual(scorer.undetected_critical_classes(self.rows(scorer, ['silent']), contracts), 1)
        self.assertEqual(scorer.undetected_critical_classes(self.rows(scorer, []), contracts), 1)
        self.assertEqual(scorer.undetected_critical_classes(self.rows(scorer, [('good', 'clean')]), contracts), 1)
        self.assertEqual(scorer.undetected_critical_classes(self.rows(scorer, [('unknown', 'disclosed'), ('unknown', 'silent')]), contracts), 1)

    def test_another_class_needs_detection(self):
        scorer = self.scorer()
        rows = self.rows(scorer, ['detected'])
        rows = [dict(r, outcome='unresolved') if r['class'] == 'LC02' else r for r in rows]
        self.assertEqual(scorer.undetected_critical_classes(rows, {'LC24': 'disclosure'}), 1)

    def test_contract_must_name_a_known_class_a_known_contract_a_reason_and_who_approved_it(self):
        scorer = self.scorer()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'budgets.json'
            good = {'class_contracts': {'LC24': {'contract': 'disclosure', 'reason': 'disclosed, not decided', 'approved_by': 'maintainer'}}, 'families': {}}
            path.write_text(json.dumps(good))
            self.assertEqual(scorer.class_contracts(scorer.read_budgets(path)), {'LC24': 'disclosure'})
            for bad in ({'LC99': {'contract': 'disclosure', 'reason': 'x'}}, {'LC24': {'contract': 'ignore', 'reason': 'x'}},
                        {'LC24': {'contract': 'disclosure'}}, {'LC24': {'contract': 'disclosure', 'reason': 'x'}},
                        {'LC24': {'contract': 'disclosure', 'reason': 'x', 'approved_by': ' '}}):
                path.write_text(json.dumps({'class_contracts': bad, 'families': {}}))
                with self.assertRaises(ValueError):
                    scorer.read_budgets(path)


if __name__ == '__main__':
    unittest.main()
