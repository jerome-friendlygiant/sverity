#!/usr/bin/env python3
"""Reviewed-contract residual measurement: five pinned public CDC IPs under reviewed SDC contracts.

Runs `<binary> --profile asic --goal cdc --deck cdc` on each known-good design from
tests/public_projects/manifest.json (detached, clean checkouts at the pinned commits under
<scratch>/public), constrained by tests/public_projects/reviewed_sdc/<design>.sdc. Counts
non-INFO diagnostics of rules marked "cdc_deck": true in docs/rules/catalog.json, classifies
them with the same conventions as tools/dev/cdc_mutation_score.py, and checks the residual
budgets from docs/rules/CDC_WORKFLOW.md#scope-and-gate.

Budgets (per analysed kLOC, unrounded aggregate rates):
  FATAL == 0 and ERROR == 0           machine-checked.
  confirmed false positives <= 1      the tool cannot decide "confirmed"; the established plus
                                      heuristic findings are reported as the unreviewed upper bound
                                      and each must be dispositioned by review.
  not analysed <= 2                   machine-checked (NOT_ANALYSED findings).
  obligations <= 15                   machine-checked over unresolved plus generic-obligation
                                      findings (the obligation / contract-dependent class).
Exit status: 0 when every machine-checked budget holds, 1 otherwise, 2 on a run failure.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location("cdc_mutation_score", ROOT / "tools/dev/cdc_mutation_score.py")
mutation = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(mutation)

_MG_SPEC = importlib.util.spec_from_file_location("mem_guard", ROOT / "tools/dev/mem_guard.py")
mem_guard = importlib.util.module_from_spec(_MG_SPEC)
_MG_SPEC.loader.exec_module(mem_guard)

CATALOG = ROOT / "docs/rules/catalog.json"
DEFAULT_SDC_DIR = ROOT / "tests/public_projects/reviewed_sdc"
SCHEMA = "sverity-cdc-reviewed-residuals/1"
FLAGS = ["--profile", "asic", "--goal", "cdc", "--deck", "cdc"]

# Physical source lines analysed per IP (top file plus the pinned modules it instantiates,
# comments included, headers excluded), from tests/accuracy/results/cdc-residual-review-rfc1.json.gz
# (lint_standard_comparison.analysed_physical_lines). The aggregate is 2.894 kLOC.
ANALYSED_LINES = {
    "axis_async_fifo": 911,
    "cc_cdc_2phase": 246,
    "cc_cdc_4phase": 511,
    "cc_cdc_fifo_2phase": 558,
    "cc_cdc_fifo_gray": 668,
}
TOTAL_LINES = 2894

SEVERITIES = ("FATAL", "ERROR", "WARNING")
CLASSES = ("established", "heuristic", "unresolved", "not_analysed", "generic_obligation",
           "waived", "resolved", "informational")
BUDGETS = dict(false_positive_per_kloc=1.0, not_analysed_per_kloc=2.0, obligation_per_kloc=15.0)


def deck_rules():
    rules = json.loads(CATALOG.read_text())["rules"]
    return {r["id"] for r in rules if r.get("cdc_deck") is True}


def run_design(binary, spec, cwd, scratch, dependencies, sdc, tag):
    scratch.mkdir(parents=True, exist_ok=True)
    report = scratch / f"{spec['name']}-{tag}.json"
    report.unlink(missing_ok=True)
    args = [*FLAGS, "--top", spec["name"], "--root", ".", "--sdc", str(sdc)]
    args += [x.format(**dependencies) for x in spec.get("args", [])]
    args += spec["sources"]
    cmd, unit = mem_guard.build_command(
        [str(binary), "--no-color", "--json", str(report), *args],
        profile="public", label=f"{spec['name']}-{tag}", exec_mode=True,
    )
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, timeout=600)
    except subprocess.TimeoutExpired as exc:
        mem_guard.stop_unit(unit)
        raise RuntimeError(f"{spec['name']}: TIMEOUT (600s)") from exc
    oom, detail = mem_guard.inspect_oom(unit, p.stderr, p.returncode,
                                        profile="public", label=f"{spec['name']}-{tag}")
    if oom:
        raise RuntimeError(f"{spec['name']}: MEMORY LIMIT EXCEEDED: {detail}")
    if not report.exists():
        raise RuntimeError(f"{spec['name']}: no report (exit {p.returncode}): {p.stderr[-500:]}")
    return json.loads(report.read_text()), p.returncode


def tally(diagnostics, deck):
    """Weighted counts of non-INFO diagnostics, split into deck and non-deck preview rules."""
    out = dict(deck=_empty(), preview=_empty(), deck_by_rule=collections.Counter(),
               preview_by_rule=collections.Counter())
    for d in diagnostics:
        if d.get("severity") == "INFO":
            continue
        group = "deck" if d.get("rule_id") in deck else "preview"
        n = int(d.get("count", 1))
        bucket = out[group]
        bucket["total"] += n
        bucket["severity"][d.get("severity", "unknown")] += n
        bucket["classification"][mutation.classify(d)] += n
        out[group + "_by_rule"][d.get("rule_id", "unknown")] += n
    return out


def _empty():
    return dict(total=0, severity=collections.Counter(), classification=collections.Counter())


def _flat(bucket):
    row = dict(non_info=bucket["total"])
    for key in SEVERITIES:
        row[key] = bucket["severity"].get(key, 0)
    for key in CLASSES:
        row[key] = bucket["classification"].get(key, 0)
    return row


def rates(row, kloc):
    defects = row["established"] + row["heuristic"]
    obligations = row["unresolved"] + row["generic_obligation"]
    return dict(non_info_per_kloc=row["non_info"] / kloc,
                false_positive_upper_bound_per_kloc=defects / kloc,
                not_analysed_per_kloc=row["not_analysed"] / kloc,
                obligation_per_kloc=obligations / kloc)


def budget_checks(total, kloc):
    r = rates(total, kloc)
    return {
        "FATAL == 0": dict(value=total["FATAL"], pass_=total["FATAL"] == 0, machine_checked=True),
        "ERROR == 0": dict(value=total["ERROR"], pass_=total["ERROR"] == 0, machine_checked=True),
        "confirmed false positives <= 1/kLOC": dict(
            value=r["false_positive_upper_bound_per_kloc"], limit=BUDGETS["false_positive_per_kloc"],
            pass_=r["false_positive_upper_bound_per_kloc"] <= BUDGETS["false_positive_per_kloc"],
            machine_checked=False,
            note="value is the unreviewed upper bound (established + heuristic); confirmation needs review"),
        "not analysed <= 2/kLOC": dict(
            value=r["not_analysed_per_kloc"], limit=BUDGETS["not_analysed_per_kloc"],
            pass_=r["not_analysed_per_kloc"] <= BUDGETS["not_analysed_per_kloc"], machine_checked=True),
        "obligations <= 15/kLOC": dict(
            value=r["obligation_per_kloc"], limit=BUDGETS["obligation_per_kloc"],
            pass_=r["obligation_per_kloc"] <= BUDGETS["obligation_per_kloc"], machine_checked=True,
            note="unresolved + generic_obligation findings"),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary", type=pathlib.Path)
    ap.add_argument("scratch", type=pathlib.Path)
    ap.add_argument("--json", type=pathlib.Path, required=True)
    ap.add_argument("--sdc-dir", type=pathlib.Path, default=DEFAULT_SDC_DIR,
                    help="directory of <design>.sdc files (default: tests/public_projects/reviewed_sdc)")
    opt = ap.parse_args()
    if opt.json.exists():
        ap.error("output already exists; choose a new JSON path")
    scratch = opt.scratch.resolve()
    if scratch.is_relative_to(ROOT):
        ap.error("scratch must be outside the repository")
    scratch.mkdir(parents=True, exist_ok=True)
    binary = opt.binary.resolve()
    sdc_dir = opt.sdc_dir.resolve()
    manifest = json.loads(mutation.MANIFEST.read_text())
    specs = [x for x in manifest["known_good"]]
    if {x["name"] for x in specs} != set(ANALYSED_LINES) or sum(ANALYSED_LINES.values()) != TOTAL_LINES:
        raise RuntimeError("manifest designs differ from the analysed-line table")
    kloc = TOTAL_LINES / 1000.0
    deck = deck_rules()
    checkouts, pins = mutation.checkouts(scratch, manifest)
    dependencies = {name: str(path) for name, path in checkouts.items()}

    designs, totals = {}, dict(deck=_empty(), preview=_empty())
    deck_rules_seen, preview_rules_seen = collections.Counter(), collections.Counter()
    for spec in specs:
        name = spec["name"]
        sdc = sdc_dir / f"{name}.sdc"
        if not sdc.is_file():
            raise RuntimeError(f"{name}: missing reviewed SDC {name}.sdc in the SDC directory")
        cwd = checkouts[spec["repository"]] / spec["workdir"]
        report, rc = run_design(binary, spec, cwd, scratch / "residual-reports", dependencies, sdc, "reviewed")
        summary = report.get("summary", {})
        if summary.get("execution_status") != "SUCCESS" or rc < 0 or rc & (8 | 64):
            raise RuntimeError(f"{name}: execution failed (exit {rc})")
        t = tally(report.get("diagnostics", []), deck)
        for group in ("deck", "preview"):
            totals[group]["total"] += t[group]["total"]
            totals[group]["severity"].update(t[group]["severity"])
            totals[group]["classification"].update(t[group]["classification"])
        deck_rules_seen.update(t["deck_by_rule"])
        preview_rules_seen.update(t["preview_by_rule"])
        row = _flat(t["deck"])
        lines = ANALYSED_LINES[name]
        designs[name] = dict(
            analysed_physical_lines=lines, commit=pins[spec["repository"]]["commit"],
            sdc=(f"tests/public_projects/reviewed_sdc/{name}.sdc" if sdc.parent == DEFAULT_SDC_DIR else f"{name}.sdc"),
            sdc_sha256=hashlib.sha256(sdc.read_bytes()).hexdigest(),
            report_sha256=hashlib.sha256((scratch / "residual-reports" / f"{name}-reviewed.json").read_bytes()).hexdigest(),
            execution_status=summary.get("execution_status"), analysis_coverage=summary.get("analysis_coverage"),
            review_status=summary.get("review_status"), exit_code=rc,
            deck=row, deck_rates=rates(row, lines / 1000.0),
            deck_by_rule=dict(sorted(t["deck_by_rule"].items())),
            preview=_flat(t["preview"]), preview_by_rule=dict(sorted(t["preview_by_rule"].items())))
    total_row = _flat(totals["deck"])
    checks = budget_checks(total_row, kloc)
    out = dict(
        schema=SCHEMA, tool="tools/dev/cdc_reviewed_residuals.py", flags=FLAGS,
        tool_sha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        manifest_sha256=hashlib.sha256(mutation.MANIFEST.read_bytes()).hexdigest(),
        catalog_sha256=hashlib.sha256(CATALOG.read_bytes()).hexdigest(),
        deck_rule_count=len(deck), analysed_physical_lines=TOTAL_LINES, kloc=kloc,
        designs=designs, aggregate=dict(deck=total_row, deck_rates=rates(total_row, kloc),
                                        deck_by_rule=dict(sorted(deck_rules_seen.items())),
                                        preview=_flat(totals["preview"]),
                                        preview_by_rule=dict(sorted(preview_rules_seen.items()))),
        budgets={k: dict(v) for k, v in checks.items()},
        budgets_machine_checked_pass=all(v["pass_"] for v in checks.values() if v["machine_checked"]),
        budgets_all_pass_if_upper_bound_confirmed=all(v["pass_"] for v in checks.values()))
    opt.json.parent.mkdir(parents=True, exist_ok=True)
    with opt.json.open("x") as output:
        output.write(json.dumps(out, indent=2, sort_keys=True) + "\n")

    print(f"{'design':<20} {'kLOC':>6} {'nonINFO':>7} {'ERR':>3} {'FATAL':>5} {'WARN':>4} "
          f"{'estab':>5} {'heur':>4} {'unres':>5} {'n/a':>3} {'oblig':>5}  preview")
    for name, d in designs.items():
        r = d["deck"]
        print(f"{name:<20} {d['analysed_physical_lines'] / 1000:>6.3f} {r['non_info']:>7} {r['ERROR']:>3} {r['FATAL']:>5} "
              f"{r['WARNING']:>4} {r['established']:>5} {r['heuristic']:>4} {r['unresolved']:>5} "
              f"{r['not_analysed']:>3} {r['generic_obligation']:>5}  {d['preview']['non_info']}")
    r = total_row
    print(f"{'AGGREGATE':<20} {kloc:>6.3f} {r['non_info']:>7} {r['ERROR']:>3} {r['FATAL']:>5} {r['WARNING']:>4} "
          f"{r['established']:>5} {r['heuristic']:>4} {r['unresolved']:>5} {r['not_analysed']:>3} "
          f"{r['generic_obligation']:>5}  {out['aggregate']['preview']['non_info']}")
    for key, value in rates(total_row, kloc).items():
        print(f"  {key}: {value!r}")
    for key, c in checks.items():
        print(f"  [{'PASS' if c['pass_'] else 'FAIL'}] {key}: {c['value']!r}"
              + ("" if c["machine_checked"] else " (unreviewed upper bound)"))
    print("machine-checked budgets:", "PASS" if out["budgets_machine_checked_pass"] else "FAIL")
    return 0 if out["budgets_machine_checked_pass"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.SubprocessError, RuntimeError, KeyError, ValueError) as exc:
        print(f"cdc reviewed residuals: {exc}", file=sys.stderr)
        sys.exit(2)
