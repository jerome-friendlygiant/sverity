#!/usr/bin/env python3
"""Pinned public RTL gate and network-free failure checks; development only."""
import argparse
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def validate(name, report, spec):
    errors = []
    summary = report.get('summary', {})
    if summary.get('execution_status') != 'SUCCESS':
        errors.append('execution_status: expected SUCCESS, got ' + str(summary.get('execution_status')))
    objects = report.get('coverage', {}).get('design_objects', {})
    bits = objects.get('register_bits', 0)
    if bits < spec['min_register_bits']:
        errors.append(f'register_bits: {bits} < {spec["min_register_bits"]}')
    for key, expected in [('analysis_coverage', 'INCOMPLETE'), ('review_status', 'REVIEW_REQUIRED')]:
        if summary.get(key) != expected:
            errors.append(f'{key}: expected {expected}, got {summary.get(key)}')
    diagnostics = report.get('diagnostics', [])
    for d in diagnostics:
        if d.get('severity') in ('FATAL', 'ERROR'):
            errors.append(f'{d.get("rule_id")} {d.get("severity")}: {d.get("message")}')
    for required in spec['required_findings']:
        def matches(d):
            return all(d.get(k) == v for k, v in required.items() if k not in ('message_contains', 'message_contains_all', 'min_count', 'signals')) and \
                ('signals' not in required or sorted(d.get('signals', [])) == required['signals']) and \
                required.get('message_contains', '') in d.get('message', '') and \
                all(part in d.get('message', '') for part in required.get('message_contains_all', [])) and \
                d.get('disposition') != 'WAIVED'
        count = sum(d.get('count', 1) for d in diagnostics if matches(d))
        minimum = required.get('min_count', 1)
        if count < minimum:
            errors.append(f'missing required finding ({count}/{minimum}): ' + json.dumps(required, sort_keys=True))
    if errors:
        print(f'FAIL {name}')
        for error in errors:
            print('  ' + error)
    else:
        print(f'OK   {name}: SUCCESS, {bits} register bits, 0 ERROR/FATAL, INCOMPLETE, REVIEW_REQUIRED; '
              f'{len(spec["required_findings"])} requirements satisfied')
    return not errors


def run_report(binary, cwd, args, output):
    output.unlink(missing_ok=True)
    process = subprocess.run([str(binary), '--no-color', '--json', str(output)] + args, cwd=cwd,
                             stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        return json.loads(output.read_text()), process.returncode
    except (OSError, json.JSONDecodeError) as exc:
        print(f'FAIL {output.stem}: missing or invalid report (exit {process.returncode}): {exc}')
        if process.stderr.strip():
            print(process.stderr.strip())
        return None, process.returncode


def goals_match_full_run(binary, cwd, args, name, scratch):
    """A goal only selects rules: what it keeps must equal the same rules in a full run (CST-019 discloses the scope)."""
    def selected(extra):
        report, _ = run_report(binary, cwd, extra + args, scratch / (name + '-goal.json'))
        return report
    full = selected([])
    if full is None: return False
    identity = lambda d: (d.get('rule_id'), d.get('fingerprint'), d.get('severity'), d.get('message'))
    ok = True
    for goal in ('lint', 'cdc', 'rdc'):
        scoped = selected(['--goal', goal])
        if scoped is None: return False
        kept = {r['rule_id'] for r in scoped['coverage']['rules'] if r['state'] != 'DISABLED'} - {'CST-019'}
        want = sorted(identity(d) for d in full['diagnostics'] if d.get('rule_id') in kept)
        got = sorted(identity(d) for d in scoped['diagnostics'] if d.get('rule_id') in kept)
        if want != got:
            ok = False
            print(f'FAIL {name}: --goal {goal} differs from a full run: {sorted(set(want) ^ set(got))[:3]}')
    return ok


def self_check(binary):
    with tempfile.TemporaryDirectory(prefix='sverity-gate-check-') as tmp:
        d = Path(tmp)
        (d / 'clocks.sdc').write_text('create_clock -name a -period 10 [get_ports a]\ncreate_clock -name b -period 7 [get_ports b]\nset_clock_groups -asynchronous -group {a} -group {b}\n')
        (d / 'error.sv').write_text('module top(input logic a,b,x, output logic q); logic src; always_ff @(posedge a) src <= x; always_ff @(posedge b) q <= src; endmodule\n')
        (d / 'invalid.sv').write_text('module top( input logic a; endmodule\n')
        (d / 'clean.sv').write_text('module top(input logic a,x, output logic q); always_ff @(posedge a) q <= x; endmodule\n')
        base = {'min_register_bits': 1, 'required_findings': []}
        for name, source, spec, extra in [
            ('error', 'error.sv', base, ['--sdc', str(d / 'clocks.sdc')]),
            ('elaboration', 'invalid.sv', base, []),
            ('missing-required', 'clean.sv', {'min_register_bits': 1, 'required_findings': [
                {'rule_id': 'CDC-039', 'severity': 'WARNING', 'confidence': 'unresolved', 'proof_state': 'NOT_ATTEMPTED'}]}, [])]:
            print(f'SELF-CHECK {name}: expected failure')
            report, rc = run_report(binary, d, ['--top', 'top'] + extra + [source], d / (name + '.json'))
            if report is None:
                # A missing report is itself a rejected execution, but only the
                # elaboration case is allowed to fail at this earlier boundary.
                if name != 'elaboration' or rc == 0: return False
            elif (name == 'error' and not any(d.get('severity') == 'ERROR' for d in report.get('diagnostics', []))) or (name == 'elaboration' and report.get('summary', {}).get('execution_status') == 'SUCCESS'):
                print(f'SELF-CHECK {name}: expected failure condition was absent')
                return False
            elif name == 'missing-required' and (report.get('summary', {}).get('execution_status') != 'SUCCESS' or report.get('summary', {}).get('analysis_coverage') != 'INCOMPLETE' or report.get('summary', {}).get('review_status') != 'REVIEW_REQUIRED' or report.get('coverage', {}).get('design_objects', {}).get('register_bits', 0) < 1 or any(d.get('severity') in ('FATAL', 'ERROR') for d in report.get('diagnostics', []))):
                print('SELF-CHECK missing-required: the control design did not otherwise pass')
                return False
            elif validate(name, report, spec):
                print(f'SELF-CHECK {name}: unexpectedly passed')
                return False
        print('SELF-CHECK PASS: all three failures rejected')
        return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--group', choices=['all', 'robustness', 'corpus', 'self-check'], default='all')
    opt = parser.parse_args()
    binary = opt.binary.resolve()
    if opt.group == 'self-check': return 0 if self_check(binary) else 1
    scratch = opt.scratch.resolve()
    if scratch.is_relative_to(ROOT):
        parser.error('scratch directory must be outside the repository')
    scratch.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((HERE / 'manifest.json').read_text())
    repos = {p['name']: p for p in manifest['repositories']}
    repos.update({p['name']: p for p in manifest['projects']})
    checked = {}
    def checkout(name):
        if name in checked: return checked[name]
        r = repos[name]; path = scratch / name
        if not path.exists(): subprocess.run(['git', 'clone', '-q', r['url'], str(path)], check=True)
        if subprocess.check_output(['git', '-C', str(path), 'status', '--porcelain'], text=True).strip():
            raise RuntimeError(f'{name}: scratch checkout contains local changes')
        subprocess.run(['git', '-C', str(path), 'checkout', '-q', '--detach', r['commit']], check=True)
        actual = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != r['commit']: raise RuntimeError(f'{name}: expected {r["commit"]}, got {actual}')
        checked[name] = path
        return path
    ok = True
    if opt.group in ('all', 'robustness'):
        for project in manifest['projects']:
            path = checkout(project['name']); start = time.monotonic()
            report, rc = run_report(binary, path, ['--exit-policy', 'violations'] + project['args'], scratch / (project['name'] + '.json'))
            elapsed = time.monotonic() - start; expect = project['expect']
            passed = report is not None and report.get('summary', {}).get('execution_status') == expect['execution_status'] and elapsed <= expect['max_seconds'] and report.get('coverage', {}).get('design_objects', {}).get('register_bits', 0) >= expect['min_register_bits']
            print(f'{"OK  " if passed else "FAIL"} {project["name"]}: robustness {elapsed:.1f}s')
            ok = passed and ok
    if opt.group in ('all', 'corpus'):
        for spec in manifest['known_good']:
            paths = {name: str(checkout(name)) for name in spec.get('dependencies', [])}
            cwd = checkout(spec['repository']) / spec['workdir']
            args = ['--top', spec['name'], '--root', '.', '--sdc', str(HERE / spec['sdc'])]
            args += [arg.format(**paths) for arg in spec['args']] + spec['sources']
            report, rc = run_report(binary, cwd, args, scratch / (spec['name'] + '.json'))
            passed = report is not None and validate(spec['name'], report, spec)
            passed = goals_match_full_run(binary, cwd, args, spec['name'], scratch) and passed
            if rc < 0 or rc & (8 | 64):
                print(f'FAIL {spec["name"]}: analyzer execution failed with exit {rc}')
                passed = False
            ok = passed and ok
    return 0 if ok else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError, RuntimeError, KeyError, ValueError) as exc:
        print(f'FAIL runner: {exc}', file=sys.stderr)
        sys.exit(1)
