import pathlib, subprocess, sys, tempfile

binary, root = sys.argv[1:]
with tempfile.TemporaryDirectory() as tmp:
    def run(args, code, *expected):
        p = subprocess.run([binary, *args.split()], cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert p.returncode == code, (args, p.returncode, p.stdout)
        for value in expected:
            assert value in p.stdout, (args, value, p.stdout)
    base = '--goal cdc --top top'
    sdc = '--sdc docs/tutorial/clocks.sdc'
    bad = 'docs/tutorial/bad.sv'
    run(f'{base} {bad}', 6, 'clock relationship UNKNOWN', 'Result: REVIEW_REQUIRED, exit 6', 'coverage   INCOMPLETE', 'review     REVIEW_REQUIRED')
    run(f'{base} {sdc} {bad}', 5, 'ERROR [CDC-003]', 'declared asynchronous')
    run(f'{base} {sdc} docs/tutorial/fixed.sv', 6, 'recognised 2-stage flop synchroniser', 'INFO [CDC-003]')
    run(f'{base} {sdc} --summary {bad}', 5, 'CDC-003')
    run(f'{base} {sdc} --verbose {bad}', 5, 'launch:', 'capture:', 'clocks:', 'path:', 'confidence:', 'proof:', 'fingerprint:')
    run('--explain CDC-003', 0, 'CDC-003')
    run(f'{base} {sdc} --group --json {tmp}/report.json --html {tmp}/report.html docs/tutorial/bigger.sv', 5, 'CDC-')
    assert pathlib.Path(tmp, 'report.json').is_file() and pathlib.Path(tmp, 'report.html').is_file()
    run(f'--config docs/tutorial/config.json --top top {bad}', 6, '(was ERROR)')
    run(f'{base} {sdc} --waivers docs/tutorial/waivers.json {bad}', 6, '<WAIVED>', 'Retain and review the waiver justification')
    run(f'{base} {sdc} docs/tutorial/waived.sv', 6, '<WAIVED>', 'Retain and review the waiver justification')
    run(f'{base} {sdc} --exit-policy violations docs/tutorial/fixed.sv', 0, 'violations policy', 'coverage   INCOMPLETE')
