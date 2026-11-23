#!/usr/bin/env python3
"""Pinned public RTL gate and network-free failure checks; development only."""
import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
_MG_SPEC = importlib.util.spec_from_file_location('mem_guard', ROOT / 'tools/dev/mem_guard.py')
mem_guard = importlib.util.module_from_spec(_MG_SPEC)
_MG_SPEC.loader.exec_module(mem_guard)


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


def run_report(binary, cwd, args, output, profile='public', label=''):
    output.unlink(missing_ok=True)
    tag = label or output.stem
    cmd, unit = mem_guard.build_command(
        [str(binary), '--no-color', '--json', str(output)] + args,
        profile=profile, label=tag, exec_mode=True,
    )
    try:
        process = subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                 text=True, timeout=300)
    except subprocess.TimeoutExpired:
        mem_guard.stop_unit(unit)
        print(f'FAIL {output.stem}: TIMEOUT (300s)')
        return None, 124
    oom, detail = mem_guard.inspect_oom(unit, process.stderr, process.returncode, profile=profile, label=tag)
    if oom:
        print(f'FAIL {output.stem}: MEMORY LIMIT EXCEEDED ({detail})')
        return None, 137
    try:
        return json.loads(output.read_text()), process.returncode
    except (OSError, json.JSONDecodeError) as exc:
        print(f'FAIL {output.stem}: missing or invalid report (exit {process.returncode}): {exc}')
        if process.stderr.strip():
            print(process.stderr.strip())
        return None, process.returncode


def check_report_replay(binary, cwd, args, name, scratch, original, original_exit):
    """Check deterministic full reports and the diagnostics retained by each goal."""
    output = scratch / (name + '.json')
    original_bytes = output.read_bytes()
    full, full_exit = run_report(binary, cwd, args, output, profile='public', label=f'{name}-replay')
    if full is None:
        return False, full_exit
    ok = full_exit == original_exit and output.read_bytes() == original_bytes
    if not ok:
        print(f'FAIL {name}: identical invocations produced different JSON reports or exit codes')
        output.with_name(name + '-first.json').write_bytes(original_bytes)

    def selected(extra, tag):
        return run_report(binary, cwd, extra + args, scratch / (name + '-goal.json'),
                          profile='public', label=f'{name}-{tag}')

    identity = lambda d: (d.get('rule_id'), d.get('fingerprint'), d.get('severity'), d.get('message'))
    for goal in ('lint', 'cdc', 'rdc'):
        scoped, scoped_rc = selected(['--goal', goal], goal)
        if scoped is None:
            return False, scoped_rc
        kept = {r['rule_id'] for r in scoped['coverage']['rules'] if r['state'] != 'DISABLED'} - {'CST-019'}
        want = sorted(identity(d) for d in original['diagnostics'] if d.get('rule_id') in kept)
        got = sorted(identity(d) for d in scoped['diagnostics'] if d.get('rule_id') in kept)
        if want != got:
            ok = False
            print(f'FAIL {name}: --goal {goal} differs from a full run: {sorted(set(want) ^ set(got))[:3]}')
    return ok, 0


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
            report, rc = run_report(binary, d, ['--top', 'top'] + extra + [source], d / (name + '.json'),
                                    profile='public', label=f'self-{name}')
            if rc == 137:
                return False
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
    oom_exit = False
    if opt.group in ('all', 'robustness'):
        for project in manifest['projects']:
            path = checkout(project['name']); start = time.monotonic()
            report, rc = run_report(binary, path, ['--exit-policy', 'violations'] + project['args'],
                                    scratch / (project['name'] + '.json'),
                                    profile='robustness', label=project['name'])
            elapsed = time.monotonic() - start; expect = project['expect']
            if rc == 137:
                oom_exit = True
            passed = report is not None and report.get('summary', {}).get('execution_status') == expect['execution_status'] and elapsed <= expect['max_seconds'] and report.get('coverage', {}).get('design_objects', {}).get('register_bits', 0) >= expect['min_register_bits']
            print(f'{"OK  " if passed else "FAIL"} {project["name"]}: robustness {elapsed:.1f}s')
            ok = passed and ok
    if opt.group in ('all', 'corpus'):
        for spec in manifest['known_good']:
            paths = {name: str(checkout(name)) for name in spec.get('dependencies', [])}
            cwd = checkout(spec['repository']) / spec['workdir']
            args = ['--top', spec['name'], '--root', '.', '--sdc', str(HERE / spec['sdc'])]
            args += [arg.format(**paths) for arg in spec['args']] + spec['sources']
            report, rc = run_report(binary, cwd, args, scratch / (spec['name'] + '.json'),
                                    profile='public', label=spec['name'])
            if rc == 137:
                oom_exit = True
            passed = report is not None and validate(spec['name'], report, spec)
            if report is not None:
                replay_ok, replay_rc = check_report_replay(binary, cwd, args, spec['name'], scratch, report, rc)
                if replay_rc == 137:
                    oom_exit = True
                passed = replay_ok and passed
            if rc < 0 or rc & (8 | 64):
                print(f'FAIL {spec["name"]}: analyzer execution failed with exit {rc}')
                passed = False
            ok = passed and ok
    if oom_exit:
        return 137
    return 0 if ok else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError, RuntimeError, KeyError, ValueError) as exc:
        print(f'FAIL runner: {exc}', file=sys.stderr)
        sys.exit(1)
