#!/usr/bin/env python3
"""Noise regression gate for the reference-IP replay: fails when a count that must only fall has risen.

    noise_gate.py SCRATCH [--expect tests/accuracy/results/noise-gate-6623e2c.json]
                          [--ledger tests/accuracy/results/lint-ledger-6623e2c.json] [--write OUT.json]

SCRATCH is the tree tools/dev/reference_lint.py wrote (SCRATCH/lint/<ip>/report.json). Per IP the gate counts, from the
report's diagnostics, the findings that block (WARNING or ERROR/FATAL, not waived, not an obligation, not a gap), the
not-analysed gaps and the declared obligations, and compares each with the expectation file. A count above its
expectation, a missing report or an execution that did not succeed fails with exit 1; a count below it only says so, and
the expectation is then tightened by hand: `--write OUT.json` prints the new file for the replay just read.

A count can hide a swap, so the blocking and not-analysed findings are also compared per site (rule, file, line) with the
triage ledger: a site the ledger does not list is NEW and fails; a site the ledger lists and the replay no longer reports
is FIXED and is only noted, except a site the ledger classes as a real defect (MUST-STAY), which fails when it disappears:
a defect is not fixed by the tool going quiet. A deliberate change updates the ledger in the same commit.

Run it after a replay, never inside tools/dev/check.sh (the replay is too heavy for that gate):

    python3 tools/dev/reference_lint.py build/sverity SCRATCH
    python3 tools/dev/noise_gate.py SCRATCH
"""
import argparse
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
DEFAULT_EXPECT = HERE.parents[1] / "tests/accuracy/results/noise-gate-6623e2c.json"
DEFAULT_LEDGER = HERE.parents[1] / "tests/accuracy/results/lint-ledger-6623e2c.json"
KINDS = ("blocking", "not_analysed", "obligations")
MUST_STAY = "real defect"


def counts(report):
    """{blocking, not_analysed, obligations} of one SVerity JSON report (a dict)."""
    out = dict.fromkeys(KINDS, 0)
    for d in report["diagnostics"]:
        disposition = d.get("disposition", "")
        if disposition == "NOT_ANALYSED":
            out["not_analysed"] += 1
        elif disposition == "OBLIGATION":
            out["obligations"] += 1
        elif d["severity"] in ("WARNING", "ERROR", "FATAL") and disposition not in ("WAIVED", "RESOLVED"):
            out["blocking"] += 1
    return out


def sites_of(report, locate):
    """{(rule, file in the checkout, line)} of the blocking and not-analysed findings of one SVerity JSON report (a dict)."""
    out = set()
    for d in report["diagnostics"]:
        disposition = d.get("disposition", "")
        blocking = d["severity"] in ("WARNING", "ERROR", "FATAL") and disposition not in ("WAIVED", "RESOLVED", "OBLIGATION")
        loc = d.get("location") or {}
        if (disposition == "NOT_ANALYSED" or blocking) and loc.get("file"):
            out.add((d["rule_id"], locate(loc["file"]), loc.get("line", 0)))
    return out


def compare_sites(ledger, current):
    """(failures, notes): `ledger` is {(rule, file, line): classification}, `current` a set of the same keys. A new site fails; a
    site that disappeared is noted, and fails when the ledger classes it as a real defect."""
    failures, notes = [], []
    for rule, file, line in sorted(current - set(ledger)):
        failures.append(f"NEW site {rule} {file}:{line}: not in the ledger, triage it")
    for key in sorted(set(ledger) - current):
        text = f"{key[0]} {key[1]}:{key[2]} ({ledger[key]})"
        if ledger[key] == MUST_STAY:
            failures.append(f"MUST-STAY site {text} disappeared without a ledger change")
        else:
            notes.append(f"FIXED site {text}")
    return failures, notes


def read_ledger(path):
    return {(x["rule"], x["file"], x["line"]): x["classification"] for x in json.loads(path.read_text())["sites"]}


def compare(expected, actual):
    """(failures, notes): a count above its expectation, an IP that did not run, or one nobody expected fails."""
    failures, notes = [], []
    for ip, want in expected.items():
        got = actual.get(ip)
        if got is None:
            failures.append(f"{ip}: no report")
            continue
        if "execution" in got:
            failures.append(f"{ip}: {got['execution']}")
            continue
        for kind in KINDS:
            if got[kind] > want[kind]:
                failures.append(f"{ip}: {kind} rose from {want[kind]} to {got[kind]}")
            elif got[kind] < want[kind]:
                notes.append(f"{ip}: {kind} fell from {want[kind]} to {got[kind]}")
    for ip in sorted(set(actual) - set(expected)):
        failures.append(f"{ip}: not in the expectation file")
    return failures, notes


def read_sites(scratch, ips):
    """{(rule, file, line)} over the IPs whose report exists, file named by its path in the OpenTitan checkout."""
    import verilator_diff   # the one path normaliser of the replay tools
    locate = verilator_diff.Locator(scratch / "opentitan")
    root = (scratch / "opentitan").resolve()
    current = set()
    for ip in ips:
        path = scratch / "lint" / ip / "report.json"
        if path.is_file():
            current |= sites_of(json.loads(path.read_text()), lambda name: locate(name, root))
    return current


def read_replay(scratch, ips):
    actual = {}
    for ip in ips:
        path = scratch / "lint" / ip / "report.json"
        if not path.is_file():
            continue
        report = json.loads(path.read_text())
        status = report["summary"]["execution_status"]
        actual[ip] = counts(report) if status == "SUCCESS" else {"execution": f"execution status {status}"}
    return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scratch", type=pathlib.Path)
    parser.add_argument("--expect", type=pathlib.Path, default=DEFAULT_EXPECT)
    parser.add_argument("--ledger", type=pathlib.Path, default=DEFAULT_LEDGER, help="the triage ledger the sites are compared with")
    parser.add_argument("--write", type=pathlib.Path, help="write an expectation file for this replay and exit")
    args = parser.parse_args()
    expectation = json.loads(args.expect.read_text())
    actual = read_replay(args.scratch.resolve(), list(expectation["ips"]))
    if args.write:
        doc = dict(expectation, ips={ip: c for ip, c in actual.items() if "execution" not in c})
        doc["totals"] = {k: sum(c[k] for c in doc["ips"].values()) for k in KINDS}
        args.write.write_text(json.dumps(doc, indent=1) + "\n")
        return 0
    failures, notes = compare(expectation["ips"], actual)
    if args.ledger:
        site_failures, site_notes = compare_sites(read_ledger(args.ledger), read_sites(args.scratch.resolve(), list(expectation["ips"])))
        failures += site_failures
        notes += site_notes
    for line in notes:
        print("lower  ", line)
    for line in failures:
        print("FAIL   ", line)
    total = {k: sum(c.get(k, 0) for c in actual.values()) for k in KINDS}
    print(f"noise gate: {'FAIL' if failures else 'PASS'} (blocking {total['blocking']}, not analysed {total['not_analysed']}, "
          f"obligations {total['obligations']}; expected {expectation['totals']['blocking']}/"
          f"{expectation['totals']['not_analysed']}/{expectation['totals']['obligations']})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
