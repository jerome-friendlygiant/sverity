#!/usr/bin/env python3
"""Differential of Verilator's reference lint against SVerity on the pinned OpenTitan IPs (docs/rules/LINT_WORKFLOW.md).

    verilator_diff.py SCRATCH [--ip uart ...] [--json OUT.json] [--triage VERDICTS.json] [--sites OUT.json] [--no-systemd] [--force]

SCRATCH is the tree tools/dev/reference_lint.py built (opentitan/, lint/<ip>/report.json, FuseSoC output). Per IP it runs
the reference flow in the IP's own lint-verilator directory, exactly as OpenTitan's lint-verilator target does:

    verilator --lint-only -f <core>.vc <VERILATOR_OPTIONS of config.mk>      (the .vc names defines, include dirs, the
                                                                             sources, top and the .vlt waiver files)

The output is cached as SCRATCH/verilator/<ip>.txt (never in the repository); --force reruns. Unless --no-systemd is
given, each run is one `systemd-run --user` unit with a 3 GB memory cap and a 900 s limit, so a runaway cannot take the
host's memory. IPs run one at a time.

Reconciliation, per Verilator warning site (file relative to the OpenTitan checkout, line, code):
  matched      in-class agreement: SVerity reports the site at the same line under a rule the crosswalk maps the code to
               (migrate_verilator.RULES_FOR_CODE) or under any rule of the code's defect classes (CLASSES_FOR_CODE, with the
               catalog's `classes`). The finding's rule, severity and disposition are stated (`matched_by`), and a match
               at INFO counts apart from one at WARNING or above (`matched_blocking`, `matched_info`);
  near         no such finding at the line, but one within NEAR lines in the same file;
  other        SVerity reports the exact line only under a rule outside the mapping (often LNT-005 for a WIDTH warning):
               reported apart, never counted as a match;
  silent       nothing at the line: a candidate miss, read in the RTL by hand;
  excluded     the code is style or simulator scheduling, outside the workflow (EXCLUDED_CODES).
`--triage` reads the verdicts of an earlier run (classification and reason per ip, code and site) so that the JSON written by
`--json` keeps the hand judgements: a site with no verdict is classified `unread`.
The other direction lists SVerity findings that block (WARNING/ERROR not waived, not Unresolved) or are not analysed and
have no Verilator warning at the site, per rule; a class Verilator does not check is named as such, not counted as noise.
Exit 0; this is a report, not a gate.
"""
import argparse
import collections
import json
import pathlib
import re
import subprocess
import sys

import migrate_verilator

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
NEAR = 3
CAP = ("-p", "MemoryMax=3G", "-p", "MemorySwapMax=0", "-p", "RuntimeMaxSec=900")

# Verilator code -> defect classes (docs/rules/LINT_WORKFLOW.md, "Defect classes", where each class names its codes).
# Rules come from RULES_FOR_CODE (the crosswalk in docs/rules/LNT.md) plus every catalog rule that lists the class.
CLASSES_FOR_CODE = {
    "WIDTH": ("LC02",), "WIDTHTRUNC": ("LC02",), "WIDTHEXPAND": ("LC02",), "WIDTHCONCAT": ("LC02",),
    "UNSIGNED": ("LC03", "LC21"), "CMPCONST": ("LC03", "LC21"),
    "SELRANGE": ("LC04",), "IMPLICIT": ("LC05",), "UNDRIVEN": ("LC06",), "MULTIDRIVEN": ("LC07", "LC20"),
    "PINMISSING": ("LC08",), "UNUSED": ("LC09",), "UNUSEDSIGNAL": ("LC09",), "UNUSEDPARAM": ("LC09",),
    "UNUSEDGENVAR": ("LC09",), "UNOPTFLAT": ("LC10",), "LATCH": ("LC11",), "NOLATCH": ("LC11",),
    "COMBDLY": ("LC12",), "BLKSEQ": ("LC12",), "BLKANDNBLK": ("LC12",), "ALWCOMBORDER": ("LC12",),
    "CASEINCOMPLETE": ("LC13",), "CASEOVERLAP": ("LC13",), "CASEX": ("LC13", "LC14"), "CASEWITHX": ("LC13", "LC14"),
    "STMTDLY": ("LC15",), "ASSIGNDLY": ("LC15",), "INITIALDLY": ("LC15",),
    "PINCONNECTEMPTY": ("LC22",), "PINNOCONNECT": ("LC08",),
    "VARHIDDEN": ("LC19",), "ASSIGNIN": ("LC01",),
}
# Codes outside the workflow, with the reason (LNT.md crosswalk "Excluded"; LINT_WORKFLOW.md "Not this workflow").
EXCLUDED_CODES = dict(migrate_verilator.NO_COUNTERPART)
EXCLUDED_CODES.update({"UNOPT": "simulator performance, not a defect", "TIMESCALEMOD": "simulation time units, not synthesis",
                       "GENUNNAMED": "style and naming", "LITENDIAN": "style (bit-order convention)"})
# Whether Verilator checks the class an SVerity-only finding belongs to (read from LINT_WORKFLOW.md's defect classes).
UNCHECKED_BY_VERILATOR = {
    "LNT-053": "dead stores (LC18)", "LNT-054": "reset-free registers read in conditions (LC16/LC23)",
    "LNT-020": "reset identity (LC16)", "LNT-068": "reset identity (LC16)", "LNT-029": "FSM states (LC13)",
    "LNT-071": "FSM reachability (LC13, an obligation)", "LNT-033": "unique/priority claims (LC13)",
    "LNT-019": "unbounded loops (LC15)", "LNT-036": "product truncation under a guard (LC02)",
    "LNT-005": "sign and conversion of a parameter override (LC03)", "LNT-041": "partial drives through a run-time index (LC06)",
    "LNT-042": "partial drives through a run-time index (LC06)", "LNT-057": "width provenance (LC02)",
    "LNT-044": "reads before writes in functions (LC11/LC12)", "CST-010": "tool precision loss, not a design property",
}
WARNING_LINE = re.compile(r"^%(Warning|Error)(?:-([A-Z0-9_]+))?: ([^:\s]+):(\d+):(\d+): (.*)$")


class Locator:
    """Names a source file by its path in the OpenTitan checkout. FuseSoC copies sources under the IP's lint directory, so
    a copy is matched to the checkout file with the same name and bytes (the longest shared path suffix wins a tie); a
    generated file with no such twin keeps `generated/<path inside the FuseSoC tree>`."""

    def __init__(self, opentitan):
        self.opentitan = opentitan.resolve()
        self.by_name = collections.defaultdict(list)
        for path in (self.opentitan / "hw").rglob("*"):
            if path.suffix in (".sv", ".svh", ".vlt", ".v") and path.is_file():
                self.by_name[path.name].append(path)
        self.cache = {}

    def __call__(self, name, base):
        path = pathlib.Path(name)
        path = (base / path).resolve() if not path.is_absolute() else path.resolve()
        if path in self.cache:
            return self.cache[path]
        result = None
        try:
            result = str(path.relative_to(self.opentitan))
        except ValueError:
            data = path.read_bytes() if path.is_file() else None
            best = (-1, None)
            for twin in self.by_name.get(path.name, []):
                if data is not None and twin.read_bytes() == data:
                    shared = len(set(reversed(twin.parts)) & set(reversed(path.parts)))
                    if shared > best[0]:
                        best = (shared, twin)
            if best[1] is not None:
                result = str(best[1].relative_to(self.opentitan))
            else:
                parts = path.parts
                result = "generated/" + "/".join(parts[parts.index("src") + 1:]) if "src" in parts else str(path.name)
        self.cache[path] = result
        return result


def lint_dir(scratch, ip):
    found = sorted((scratch / "lint" / ip).glob("*/lint-verilator/*.eda.yml"))
    if not found:
        sys.exit(f"{ip}: no FuseSoC lint setup under {scratch / 'lint' / ip}; run tools/dev/reference_lint.py first")
    return found[0].parent


def reference_command(directory):
    """The `lint-only` recipe of the Edalize Makefile with its config.mk values, as one argument list."""
    config = dict(re.findall(r"^(\w+)\s*:?=\s*(.*?)\s*$", (directory / "config.mk").read_text(), re.M))
    return ["verilator", "--lint-only", "-f", config["VC_FILE"], *config.get("VERILATOR_OPTIONS", "").split()]


def run_reference(scratch, ip, systemd, force):
    cache = scratch / "verilator" / f"{ip}.txt"
    if cache.exists() and not force:
        return cache.read_text()
    directory = lint_dir(scratch, ip)
    command = reference_command(directory)
    if systemd:
        command = ["systemd-run", "--user", "--wait", "--collect", "--pipe", "--same-dir", "-E", "PATH", "-E", "HOME",
                   *CAP, f"--unit=verilator-diff-{ip}-{__import__('os').getpid()}", "--", *command]
    done = subprocess.run(command, cwd=directory, capture_output=True, text=True)
    text = done.stderr + done.stdout
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_text(text)
    return text


def parse_warnings(text, directory, locate):
    """[(code, severity, file relative to the OpenTitan checkout, line, message)], deduplicated by site and code."""
    out, seen = [], set()
    for line in text.splitlines():
        m = WARNING_LINE.match(line)
        if not m:
            continue
        severity, code, name, number, _, message = m.groups()
        if code is None and "Exiting due to" in message:
            continue
        rel = locate(name, directory)
        key = (code or "ERROR", rel, int(number))
        if key not in seen:
            seen.add(key)
            out.append((code or "ERROR", severity, rel, int(number), message))
    return out


def svrity_sites(report, scratch, locate):
    """[(rule, severity, disposition, confidence, rel file, line, message)] for one SVerity report."""
    out = []
    root = (scratch / "opentitan").resolve()
    for d in json.loads(report.read_text())["diagnostics"]:
        loc = d.get("location") or {}
        if not loc.get("file"):
            continue
        rel = locate(loc["file"], root)
        out.append((d["rule_id"], d["severity"], d.get("disposition", ""), d.get("confidence", ""), rel, loc.get("line", 0),
                    d["message"], d.get("origin", "")))
    return out


def catalog_rules_for_class():
    by = collections.defaultdict(set)
    for rule in json.loads((ROOT / "docs/rules/catalog.json").read_text())["rules"]:
        for k in rule.get("classes", []):
            by[k].add(rule["id"])
    return by


def mapped_rules(code, by_class):
    crosswalk = set(migrate_verilator.RULES_FOR_CODE.get(code, ()))
    wide = set(crosswalk)
    for k in CLASSES_FOR_CODE.get(code, ()):
        wide |= by_class.get(k, set())
    return crosswalk, wide


def reconcile(ip, warnings, sv, by_class):
    by_site = collections.defaultdict(list)
    by_file = collections.defaultdict(list)
    for f in sv:
        by_site[(f[4], f[5])].append(f)
        by_file[f[4]].append(f)
    rows, covered = [], set()
    for code, _, rel, line, message in warnings:
        crosswalk, wide = mapped_rules(code, by_class)
        row = {"ip": ip, "code": code, "file": rel, "line": line, "message": message}
        if code in EXCLUDED_CODES:
            row["status"], row["note"] = "excluded", EXCLUDED_CODES[code]
        elif not wide:
            row["status"], row["note"] = "silent", "code has no mapped class or rule"
        else:
            hit = [f for f in by_site.get((rel, line), []) if f[0] in wide]
            if hit:
                row["status"] = "matched"
                row["rules"] = sorted({f[0] for f in hit})
                row["matched_by"] = sorted({f"{f[0]}/{f[1]}/{f[2]}" for f in hit})
                row["blocking"] = any(f[1] in ("WARNING", "ERROR", "FATAL") and f[2] not in ("WAIVED", "RESOLVED") for f in hit)
                row["crosswalk"] = bool({f[0] for f in hit} & crosswalk)
                covered.update((f[4], f[5], f[0]) for f in hit)
            else:
                near = [f for f in by_file.get(rel, []) if f[0] in wide and abs(f[5] - line) <= NEAR]
                if near:
                    row["status"], row["rules"] = "near", sorted({f"{f[0]}@{f[5]}" for f in near})
                else:
                    row["status"] = "silent"
            if row["status"] != "matched":
                here = sorted({f"{f[0]}/{f[1]}/{f[2]}" for f in by_site.get((rel, line), [])})
                row["sverity_here"] = here
                if row["status"] == "silent" and here:
                    row["status"] = "other"
        rows.append(row)
    return rows


def sverity_only(ip, warnings, sv):
    """Blocking and not-analysed SVerity findings whose site (file, line) has no Verilator warning."""
    verilator_sites = {(w[2], w[3]) for w in warnings}
    out = []
    for rule, severity, disposition, confidence, rel, line, message, origin in sv:
        blocking = severity in ("WARNING", "ERROR", "FATAL") and disposition not in ("WAIVED", "OBLIGATION", "NOT_ANALYSED", "RESOLVED")
        if disposition == "NOT_ANALYSED":
            kind = "not-analysed"
        elif blocking:
            kind = "blocking"
        else:
            continue
        if (rel, line) in verilator_sites:
            continue
        out.append({"ip": ip, "kind": kind, "rule": rule, "file": rel, "line": line, "message": message,
                    "verilator_checks_class": rule not in UNCHECKED_BY_VERILATOR})
    return out


def read_verdicts(path):
    """{(ip, code, "file:line"): site entry} from the sites of an earlier --json file, with its header fields."""
    old = json.loads(path.read_text())
    return {(x["ip"], x["code"], x["site"]): x for x in old.get("sites", [])}


# Classes a verdict can state; a status the tool computes is not a verdict.
OTHER_RULE = "reported under another rule (not a match)"


def document(summary, rows, only, verdicts, triage):
    """The committed JSON: per-IP counts, every site with its status, rule, severity and verdict, and the SVerity-only direction."""
    header = {}
    if triage:
        old = json.loads(triage.read_text())
        header = {k: old[k] for k in ("schema", "tool_commit", "verilator", "flow", "caveat", "real_misses") if k in old}
    sites = []
    for r in rows:
        key = (r["ip"], r["code"], f"{r['file']}:{r['line']}")
        site = {"ip": r["ip"], "code": r["code"], "site": key[2], "status": r["status"]}
        for field in ("matched_by", "blocking", "sverity_here"):
            if field in r:
                site[field] = r[field]
        verdict = verdicts.get(key, {})
        site["classification"] = OTHER_RULE if r["status"] == "other" else verdict.get("classification", "matched" if r["status"] == "matched" else "unread")
        if verdict.get("reason"):
            site["reason"] = verdict["reason"]
        sites.append(site)
    by_class = collections.Counter(x["classification"] for x in sites)
    by_code = collections.defaultdict(collections.Counter)
    for x in sites:
        by_code[x["code"]][x["classification"]] += 1
    totals = {k: sum(s[k] for s in summary) for k in ("verilator", "matched", "matched_blocking", "matched_info", "near", "other", "excluded", "silent")}
    bysite = collections.defaultdict(set)
    for o in only:
        bysite[(o["kind"], o["rule"])].add((o["file"], o["line"]))
    sverity_only = [{"kind": kind, "rule": rule, "sites": len(s), "verilator_checks_class": rule not in UNCHECKED_BY_VERILATOR,
                     "class_note": UNCHECKED_BY_VERILATOR.get(rule, "Verilator checks this class"),
                     "locations": [f"{f}:{n}" for f, n in sorted(s)]} for (kind, rule), s in sorted(bysite.items())]
    return {**header, "per_ip": [{k: v for k, v in s.items() if k not in ("silent_by_code", "sverity_only")} for s in summary],
            "totals": totals, "classification": dict(by_class), "by_code": {c: dict(v) for c, v in sorted(by_code.items())},
            "agreement": "in-class agreement is `matched` (a mapped rule at the site); a site reported only under another rule is counted apart",
            "sites": sites, "sverity_only": sverity_only}


def table(rows, header):
    widths = [max(len(str(r[i])) for r in [header] + rows) for i in range(len(header))]
    line = lambda r: "| " + " | ".join(str(c).ljust(w) for c, w in zip(r, widths)) + " |"
    return "\n".join([line(header), "|" + "|".join("-" * (w + 2) for w in widths) + "|"] + [line(r) for r in rows])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scratch", type=pathlib.Path)
    parser.add_argument("--ip", action="append")
    parser.add_argument("--json", type=pathlib.Path, help="write the per-IP and total counts and the site keys here")
    parser.add_argument("--triage", type=pathlib.Path, help="a JSON written earlier by --json: its per-site verdicts are carried into --json")
    parser.add_argument("--sites", type=pathlib.Path, help="write every reconciled site (file, line, code, status) here")
    parser.add_argument("--no-systemd", action="store_true", help="run Verilator directly (no memory cap)")
    parser.add_argument("--force", action="store_true", help="rerun Verilator although its output is cached")
    args = parser.parse_args()
    scratch = args.scratch.resolve()
    locate = Locator(scratch / "opentitan")
    pin = json.loads((ROOT / "tests/public_projects/lint_reference.json").read_text())
    by_class = catalog_rules_for_class()
    summary, all_rows, only = [], [], []
    for ip in args.ip or pin["ips"]:
        text = run_reference(scratch, ip, not args.no_systemd, args.force)
        warnings = parse_warnings(text, lint_dir(scratch, ip), locate)
        sv = svrity_sites(scratch / "lint" / ip / "report.json", scratch, locate)
        rows = reconcile(ip, warnings, sv, by_class)
        all_rows += rows
        only += sverity_only(ip, warnings, sv)
        count = collections.Counter(r["status"] for r in rows)
        summary.append({"ip": ip, "verilator": len(rows), "matched": count["matched"],
                        "matched_blocking": sum(1 for r in rows if r["status"] == "matched" and r["blocking"]),
                        "matched_info": sum(1 for r in rows if r["status"] == "matched" and not r["blocking"]),
                        "near": count["near"], "other": count["other"], "excluded": count["excluded"], "silent": count["silent"],
                        "silent_by_code": dict(collections.Counter(r["code"] for r in rows if r["status"] == "silent")),
                        "sverity_only": dict(collections.Counter(f"{o['kind']}:{o['rule']}" for o in only if o["ip"] == ip))})
    header = ["IP", "Verilator", "matched", "(blocking)", "(info)", "near", "other rule", "excluded", "silent"]
    keys = ("verilator", "matched", "matched_blocking", "matched_info", "near", "other", "excluded", "silent")
    rows = [[s["ip"]] + [s[k] for k in keys] for s in summary]
    rows.append(["total"] + [sum(s[k] for s in summary) for k in keys])
    print(table(rows, header))
    print("\nsilent by code:", dict(collections.Counter(r["code"] for r in all_rows if r["status"] == "silent")))
    print("near by code:", dict(collections.Counter(r["code"] for r in all_rows if r["status"] == "near")))
    print("matched only by a class rule, not the crosswalk:",
          dict(collections.Counter(r["code"] for r in all_rows if r["status"] == "matched" and not r["crosswalk"])))
    print("\nSVerity blocking / not analysed with no Verilator warning at the site, per rule (distinct sites across IPs):")
    sites = collections.defaultdict(set)
    for o in only:
        sites[(o["kind"], o["rule"])].add((o["file"], o["line"]))
    for (kind, rule), s in sorted(sites.items()):
        print(f"  {kind:13} {rule:8} {len(s):3}  {UNCHECKED_BY_VERILATOR.get(rule, 'Verilator checks this class: read the sites')}")
    if args.json:
        verdicts = read_verdicts(args.triage) if args.triage else {}
        args.json.write_text(json.dumps(document(summary, all_rows, only, verdicts, args.triage), indent=1) + "\n")
    if args.sites:
        args.sites.write_text(json.dumps({"verilator": all_rows, "sverity_only": only}, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
