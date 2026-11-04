"""Finalized machine accounting and human destination/colour contracts."""
import collections
import errno
import json
import os
from pathlib import Path
import pty
import subprocess
import sys
import tempfile
import threading

binary, root = sys.argv[1:]
args = ['--goal', 'cdc', '--top', 'top', '--sdc', 'docs/tutorial/clocks.sdc', 'docs/tutorial/bad.sv']

def run(extra, **kwargs):
    return subprocess.run([binary, *args, *extra], cwd=root, **kwargs)

def complete(records):
    return bool(records and records[-1]['type'] == 'run_end' and
                records[-1]['complete'] and records[-1]['record_count'] == len(records))

with tempfile.TemporaryDirectory(prefix='sverity-interfaces-', dir=os.environ.get('SVERITY_SCRATCH')) as tmp:
    report, sarif = Path(tmp) / 'report.json', Path(tmp) / 'report.sarif'
    proc = run(['--json', str(report), '--sarif', str(sarif), '--jsonl', '-', '--statistics', '--no-color'], capture_output=True, text=True)
    data = json.loads(report.read_text())
    events = [json.loads(line) for line in proc.stdout.splitlines()]
    assert complete(events) and not complete(events[:-1])
    assert [e['sequence'] for e in events] == list(range(1, len(events) + 1))
    assert all(e['schema'] == 'sverity-events/1' and e['schema_version'] == '1.0.0' for e in events)
    assert events[0]['tool'] == data['tool'] and events[0]['inputs'] == data['inputs']
    assert [e['diagnostic'] for e in events if e['type'] == 'finding'] == data['diagnostics']
    assert [e['coverage'] for e in events if e['type'] == 'coverage'] == [data['coverage']]
    assert [e['waiver'] for e in events if e['type'] == 'waiver'] == data['waivers']
    end = events[-1]
    assert end['summary'] == data['summary'] and end['exit_code'] == proc.returncode
    assert end['finding_records'] == len(data['diagnostics'])
    assert end['occurrences'] == sum(d['count'] for d in data['diagnostics'])
    sar = json.loads(sarif.read_text())['runs'][0]
    assert len(sar['results']) == len(data['diagnostics'])
    for result, diagnostic in zip(sar['results'], data['diagnostics']):
        related = result.get('relatedLocations', [])
        assert len(related) == len(diagnostic['evidence'])
        for loc, evidence in zip(related, diagnostic['evidence']):
            assert loc['message']['text'] == evidence['kind'] + ': ' + evidence['text']
            if evidence['location']:
                assert loc['physicalLocation']['artifactLocation']['uri'] == evidence['location']['file']
                assert loc['physicalLocation']['region']['startLine'] == evidence['location']['line']
    crossing = next(d for d in data['diagnostics'] if d['rule_id'] == 'CDC-003')
    assert next(e for e in crossing['evidence'] if e['kind'] == 'launch_register')['location']
    rule = next(r for r in sar['tool']['driver']['rules'] if r['id'] == 'CDC-003')
    assert 'Unknown' in rule['help']['text'] and 'Trigger' in rule['help']['text']
    expected = collections.defaultdict(lambda: [0] * 4)
    for d in data['diagnostics']:
        expected[d['rule_id']][['FATAL', 'ERROR', 'WARNING', 'INFO'].index(d['severity'])] += d['count']
    table = proc.stderr.split('RULE FATAL ERROR WARNING INFO\n')[1].split('Result:')[0]
    rows = [(x.split()[0], list(map(int, x.split()[1:]))) for x in table.splitlines() if x]
    assert dict(rows) == dict(expected)
    assert rows == sorted(rows, key=lambda r: (*[-n for n in r[1]], r[0]))
    assert '\x1b' not in proc.stdout + proc.stderr

    policy = run(['--jsonl', '-', '--exit-policy', 'violations'], capture_output=True, text=True)
    policy_end = json.loads(policy.stdout.splitlines()[-1])
    assert policy_end['exit_policy'] == 'violations' and policy_end['exit_code'] == policy.returncode == 1
    assert policy_end['summary'] == data['summary']
    failed = run(['--json', str(Path(tmp) / 'missing' / 'report.json'), '--jsonl', '-'], capture_output=True, text=True)
    failed_end = json.loads(failed.stdout.splitlines()[-1])
    assert failed_end['exit_code'] == failed.returncode == (proc.returncode | 8)
    assert failed_end['summary']['execution_status'] == 'ERROR'

    # A failed write is an execution error in every report saved, before or after the failure.
    saved_json, saved_sarif, saved_html = (Path(tmp) / f'saved.{x}' for x in ('json', 'sarif', 'html'))
    failed = run(['--json', str(saved_json), '--sarif', str(Path(tmp) / 'missing' / 'r.sarif'), '--html', str(saved_html)], capture_output=True)
    assert failed.returncode == (proc.returncode | 8)
    assert json.loads(saved_json.read_text())['summary']['execution_status'] == 'ERROR'
    assert 'a report could not be written' in saved_html.read_text()
    failed = run(['--sarif', str(saved_sarif), '--json', str(Path(tmp) / 'missing' / 'r.json')], capture_output=True)
    assert json.loads(saved_sarif.read_text())['runs'][0]['invocations'][0]['executionSuccessful'] is False

    # Waiving a crossing accepts its clocks: moving the capture to another clock on the same line stales it.
    design, sdc = Path(tmp) / 'clock_change.sv', Path(tmp) / 'clock_change.sdc'
    sdc.write_text('create_clock -name a -period 10 [get_ports a]\ncreate_clock -name b -period 7 [get_ports b]\n'
                   'create_clock -name c -period 3 [get_ports c]\n'
                   'set_clock_groups -asynchronous -group {a} -group {b} -group {c}\n')
    def crossing_fingerprint(capture):
        design.write_text('module top(input logic a, b, c, d, output logic q);\n logic s;\n always_ff @(posedge a) s <= d;\n'
                          f' always_ff @(posedge {capture}) q <= s;\nendmodule\n')
        out = Path(tmp) / 'clock_change.json'
        subprocess.run([binary, '--top', 'top', '--sdc', str(sdc), '--json', str(out), str(design)], capture_output=True)
        return next(x for x in json.loads(out.read_text())['diagnostics'] if x['rule_id'] == 'CDC-003')['fingerprint']
    assert crossing_fingerprint('b') != crossing_fingerprint('c')

# Exercise the actual human stream's TTY, independently of the machine stdout.
def terminal(machine, tty_stderr, color, no_color):
    master, slave = pty.openpty()
    env = dict(os.environ, NO_COLOR=no_color)
    extra = ['--color', color] + (['--jsonl', '-'] if machine else [])
    p = subprocess.Popen([binary, *args, *extra], cwd=root, env=env,
                         stdout=subprocess.PIPE if tty_stderr else slave,
                         stderr=slave if tty_stderr else subprocess.PIPE)
    os.close(slave)
    chunks = []
    def read_terminal():
        while True:
            try:
                part = os.read(master, 65536)
                if not part:
                    break
                chunks.append(part)
            except OSError as e:
                if e.errno != errno.EIO:
                    raise
                break
    reader = threading.Thread(target=read_terminal)
    reader.start()
    piped, err = p.communicate()
    reader.join()
    os.close(master)
    if machine:
        machine_data = piped if tty_stderr else b''.join(chunks)
        assert complete([json.loads(line) for line in machine_data.splitlines()])
        assert b'\x1b' not in machine_data
    return b'\x1b[' in b''.join(chunks), b'\x1b[' in (err or b'')

assert terminal(True, True, 'auto', '')[0]
assert not terminal(True, False, 'auto', '')[1]  # stdout TTY, human stderr pipe
assert terminal(False, False, 'auto', '')[0]
assert not terminal(True, True, 'auto', '1')[0]
assert terminal(True, True, 'always', '1')[0]
assert not terminal(True, True, 'never', '')[0]
invalid = run(['--color', 'sometimes'], capture_output=True)
assert invalid.returncode == 64
conflict = run(['--json', '-', '--jsonl', '-'], capture_output=True)
assert conflict.returncode == 64 and not conflict.stdout
print('interfaces: machine reconciliation, truncation, statistics and colour PASS')
