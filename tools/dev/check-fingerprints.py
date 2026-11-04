#!/usr/bin/env python3
"""Audit finding identity over fixtures and the public corpus cloned by check.sh."""
import argparse
import collections
import json
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--public', type=Path, required=True)
    opt = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    binary, out, public = opt.binary.resolve(), opt.scratch.resolve(), opt.public.resolve()
    out.mkdir(parents=True, exist_ok=True)
    runs = []
    fixtures = root / 'tests/fixtures'
    for source in sorted(fixtures.rglob('*')):
        if source.suffix not in ('.sv', '.v'):
            continue
        args = []
        for line in source.read_text().splitlines():
            if '// sverity-args: ' in line:
                extra = line.split('// sverity-args: ', 1)[1]
                args += extra.replace('@DIR@', str(source.parent)).replace('@FIXTURES@', str(fixtures)).split()
        runs.append((str(source.relative_to(root)), root, args + [str(source)]))
    manifest = json.loads((root / 'tests/public_projects/manifest.json').read_text())
    # The robustness entry defines PicoRV32's full-design invocation.
    for project in manifest['projects']:
        if project['name'] == 'picorv32':
            runs.append(('picorv32', public / project['name'], project['args']))
    for spec in manifest['known_good']:
        paths = {name: str(public / name) for name in spec.get('dependencies', [])}
        args = ['--top', spec['name'], '--root', '.', '--sdc', str(root / 'tests/public_projects' / spec['sdc'])]
        args += [arg.format(**paths) for arg in spec['args']] + spec['sources']
        runs.append((spec['name'], public / spec['repository'] / spec['workdir'], args))
    collisions = total = 0
    for index, (name, cwd, args) in enumerate(runs):
        dest = out / (str(index) + '.json')
        dest.unlink(missing_ok=True)
        process = subprocess.run([str(binary), '--no-color', '--json', str(dest)] + args, cwd=cwd,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if not dest.exists() or process.returncode < 0 or process.returncode & 64:
            raise RuntimeError(f'{name}: report execution failed ({process.returncode}): {process.stderr}')
        diagnostics = json.loads(dest.read_text())['diagnostics']
        total += len(diagnostics)
        seen = collections.defaultdict(list)
        for diagnostic in diagnostics:
            if not diagnostic.get('fingerprint'):
                raise RuntimeError(f'{name}: missing fingerprint')
            seen[diagnostic['fingerprint']].append(diagnostic)
        for fingerprint, group in seen.items():
            if len(group) > 1:
                collisions += 1
                print('COLLISION', name, fingerprint, group[0]['rule_id'], len(group), flush=True)
    print(f'{len(runs)} runs; {total} findings; {collisions} collision groups', flush=True)
    return int(bool(collisions))


if __name__ == '__main__':
    raise SystemExit(main())
