#!/usr/bin/env python3
"""Scores a SVerity binary against the accuracy suite (tests/accuracy).

    accuracy.py SVERITY [--suite DIR] [--json OUT] [--budgets FILE] [--check]
    accuracy.py --labels-only [--suite DIR]    validate labels without running anything (ctest does this)

Only WARNING/ERROR/FATAL findings of the case's family count, at a labelled site: on a site line (+-1), on the header of
the procedure containing it, or naming a site's signal (an SDC clock name also stands for the object it is declared
on). CST-019, RDC-012 and RDC-013 are stated for every run and do not count. A bad case is detected, unresolved (only
an unresolved WARNING) or missed; a good case is clean, noise, unresolved or a false error; an unknown case is
disclosed or silent (a CST unknown may also be disclosed by CDC/RDC: "elsewhere"). --check fails on a critical case
that is missed or silent, invalid/unsettled labels, an empty suite or unexpected execution failures.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import tempfile

FAMILIES = ("LNT", "CDC", "RDC", "CST")
VERDICTS = ("bad", "good", "unknown")
# Stated whatever the design does: rule coverage, unsupported power intent, reset timing that RTL can never show.
RUN_LEVEL = {"CST-019", "RDC-012", "RDC-013"}
SLACK = 1                 # a procedure header and its assignment can both be reported


def leaf(name):
    """`top.u.sync_q[3]` -> `sync_q`."""
    return re.sub(r"\[.*?\]", "", name).split(".")[-1]


def label_problems(case, label):
    found = []
    for key in ("family", "top", "verdict", "sites", "labels", "author"):
        if key not in label:
            found.append(f"{case}: label has no '{key}'")
    if label.get("family") not in FAMILIES:
        found.append(f"{case}: family must be one of {', '.join(FAMILIES)}")
    if label.get("verdict") not in VERDICTS:
        found.append(f"{case}: verdict must be one of {', '.join(VERDICTS)}")
    if label.get("verdict") in ("bad", "unknown") and not label.get("sites"):
        found.append(f"{case}: a {label.get('verdict')} case needs sites")
    for site in label.get("sites", []):
        if "line" not in site and "signal" not in site:
            found.append(f"{case}: a site needs a line or a signal")
    for entry in label.get("labels", []):
        if entry.get("verdict") not in VERDICTS:
            found.append(f"{case}: labeller {entry.get('by')} gives no valid verdict")
    return found


def provenance_problems(case, label, case_dir):
    """An adapted case names its source and keeps the attribution in the design header."""
    if label.get("origin", "authored") == "authored":
        return []
    found = []
    prov = label.get("provenance") or {}
    for key in ("repository", "commit", "path", "licence", "adaptation"):
        if not prov.get(key):
            found.append(f"{case}: adapted, but provenance has no '{key}'")
    header = "\n".join((case_dir / "design.sv").read_text(errors="replace").splitlines()[:15])
    if prov.get("path") and pathlib.PurePath(prov["path"]).name not in header:
        found.append(f"{case}: design.sv header does not name its source file '{prov['path']}'")
    return found


def disputed(label):
    """Labellers disagree on the verdict and no settling reading is recorded. Criticality is the case's own field, which
    is critical when any labeller says so."""
    if label.get("settled_by"):
        return False
    verdicts = {entry.get("verdict") for entry in label.get("labels", [])}
    return len(label.get("labels", [])) < 2 or len(verdicts) > 1


def run(binary, case_dir, label):
    with tempfile.TemporaryDirectory() as scratch:
        report = pathlib.Path(scratch) / "report.json"
        args = [str(binary), "--top", label["top"], "--root", str(case_dir), "--json", str(report), "--max-console", "1"]
        for sdc in label.get("sdc", []):
            args += ["--sdc", str(case_dir / sdc)]
        args.append(str(case_dir / "design.sv"))
        done = subprocess.run(args, capture_output=True, text=True, timeout=300)
        if not report.exists():
            return {"execution": "NO REPORT", "exit": done.returncode, "diagnostics": []}
        data = json.loads(report.read_text())
        return {"execution": data["summary"]["execution_status"], "exit": done.returncode,
                "coverage": data["summary"]["analysis_coverage"], "diagnostics": data["diagnostics"]}


def procedure_spans(text):
    """Line spans (first, last) of each `always*`/`initial` procedure, by begin/end nesting. A procedure without
    `begin` ends at its first `;` line. Good enough for the suite's small designs, which is all it is used on."""
    lines = text.splitlines()
    spans = []
    for start, line in enumerate(lines, 1):
        if not re.match(r"\s*(always\w*|initial)\b", line):
            continue
        depth, seen_begin = 0, False
        for end in range(start, len(lines) + 1):
            code = re.sub(r"//.*", "", lines[end - 1])
            opened = len(re.findall(r"\b(begin|case[xz]?|fork)\b", code))
            closed = len(re.findall(r"\b(end|endcase|join\w*)\b", code))
            depth += opened - closed
            seen_begin = seen_begin or opened > 0
            if (seen_begin and depth <= 0) or (not seen_begin and ";" in code):
                break
        spans.append((start, end))
    return spans


def clock_objects(case_dir, label):
    """SDC clock name -> leaf names of the objects it is declared on, so a site naming a clock matches its port."""
    objects = {}
    for sdc in label.get("sdc", []):
        for line in (case_dir / sdc).read_text().splitlines():
            name = re.search(r"-name\s+(\S+)", line)
            if line.lstrip().startswith(("create_clock", "create_generated_clock")) and name:
                targets = re.findall(r"\[get_(?:ports|pins|nets)\s+\{?([^\]\}]+)\}?\]\s*$", line)
                objects.setdefault(name.group(1), set()).update(leaf(t.strip().replace("/", ".")) for t in targets)
    return objects


def at_site(diagnostic, sites, spans, aliases):
    places = [diagnostic.get("location", {})] + [e.get("location", {}) for e in diagnostic.get("evidence", [])]
    names = {leaf(s) for s in diagnostic.get("signals", [])}
    if not diagnostic.get("location"):   # an unlocated finding names what it is about in its message
        names |= {leaf(w) for w in re.findall(r"[A-Za-z_][\w.$\[\]]*", diagnostic.get("message", ""))}
    for site in sites:
        wanted = site.get("file", "design.sv")
        for place in places:
            if not place or "line" not in site or pathlib.PurePath(place.get("file", "")).name != wanted:
                continue
            line = place.get("line", 0)
            if abs(line - site["line"]) <= SLACK:
                return True
            # A finding located at a procedure's header covers the procedure (the RFC locates CDC-003 and others there).
            if wanted == "design.sv" and any(first == line and first <= site["line"] <= last for first, last in spans.get(wanted, [])):
                return True
        if "signal" in site and ({site["signal"]} | aliases.get(site["signal"], set())) & names:
            return True
    return False


def sites_of(label):
    """The sites every labeller gave: several sites mean any of them."""
    sites = list(label.get("sites", []))
    for entry in label.get("labels", []):
        sites += [site for site in entry.get("sites", []) if site not in sites]
    return sites


def score(label, result, spans, aliases):
    family = label["family"]
    label = {**label, "sites": sites_of(label)}
    raised = [d for d in result["diagnostics"]
              if d["rule_id"] not in RUN_LEVEL and d["severity"] in ("WARNING", "ERROR", "FATAL")]
    mine = [d for d in raised if d["rule_id"].startswith(family + "-")]
    unresolved = lambda d: d["severity"] == "WARNING" and d.get("confidence") == "unresolved"
    # Only uncertainty about constraints is disclosed by the analysis they govern: a CST unknown case may be disclosed by a
    # CDC or RDC finding at its site. A defect needs its own family, and a site-name match elsewhere proves nothing.
    elsewhere = [d for d in raised if d not in mine and family == "CST" and d["rule_id"][:3] in ("CDC", "RDC")
                 and at_site(d, label.get("sites", []), spans, aliases)]
    verdict = label["verdict"]
    if verdict == "bad":
        hits = [d for d in mine if at_site(d, label["sites"], spans, aliases)]
        outcome = "detected" if any(not unresolved(d) for d in hits) else "unresolved" if hits else "missed"
    elif verdict == "good":
        outcome = ("false error" if any(d["severity"] in ("ERROR", "FATAL") for d in mine)
                   else "noise" if any(not unresolved(d) for d in mine)
                   else "unresolved" if mine else "clean")
        hits = mine
    else:
        hits = [d for d in mine if at_site(d, label["sites"], spans, aliases)]
        outcome = "disclosed" if hits else "elsewhere" if elsewhere else "silent"
    return {
        "outcome": outcome,
        "rules": sorted({d["rule_id"] for d in (hits or (elsewhere if outcome == "elsewhere" else []))}),
        "review_findings": len(raised),
        "unresolved_findings": sum(1 for d in raised if unresolved(d)),
    }


def silent_critical(label, outcome):
    return bool(label.get("critical")) and outcome in ("missed", "silent")


def tabulate(rows):
    table = {}
    for family in FAMILIES:
        mine = [r for r in rows if r["family"] == family and not r["disputed"]]
        counts = {}
        for r in mine:
            counts.setdefault(r["verdict"], {}).setdefault(r["outcome"], 0)
            counts[r["verdict"]][r["outcome"]] += 1
        bad, good, unknown = (sum(counts.get(v, {}).values()) for v in VERDICTS)
        rate = lambda n, d: round(n / d, 3) if d else None
        c = lambda v, o: counts.get(v, {}).get(o, 0)
        table[family] = {
            "cases": len(mine), "disputed": sum(1 for r in rows if r["family"] == family and r["disputed"]),
            "counts": counts,
            "missed_rate": rate(c("bad", "missed"), bad),
            "unresolved_bad_rate": rate(c("bad", "unresolved"), bad),
            "false_error_rate": rate(c("good", "false error"), good),
            "false_alarm_rate": rate(c("good", "false error") + c("good", "noise"), good),
            "unresolved_good_rate": rate(c("good", "unresolved"), good),
            "silent_unknown": c("unknown", "silent") + c("unknown", "elsewhere"),
            "mean_review_findings": rate(sum(r["review_findings"] for r in mine), len(mine)),
        }
    return table


def against(table, budgets):
    """Each budget and whether the measurement is within it."""
    verdicts = []
    for family, limits in budgets.get("families", {}).items():
        measured = table.get(family, {})
        for key, limit in limits.items():
            metric = key.removeprefix("max_")
            value = measured.get(metric)
            verdicts.append({"family": family, "metric": metric, "limit": limit, "measured": value,
                             "within": None if value is None else value <= limit})
    return verdicts


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", type=pathlib.Path, nargs="?")
    parser.add_argument("--suite", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[2] / "tests/accuracy")
    parser.add_argument("--json", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--labels-only", action="store_true")
    parser.add_argument("--budgets", type=pathlib.Path, help='limits to compare with: {"families": {"CDC": {"max_missed_rate": 0.05}}}')
    args = parser.parse_args()
    label_paths = sorted((args.suite / "cases").glob("*/*/label.json"))
    if args.labels_only:   # held out: the suite's results are measured on purpose, not on every test run
        problems = [] if label_paths else ["accuracy suite contains no cases"]
        for label_path in label_paths:
            name = f"{label_path.parent.parent.name}/{label_path.parent.name}"
            label = json.loads(label_path.read_text())
            problems += label_problems(name, label)
            problems += provenance_problems(name, label, label_path.parent)
            if label.get("verdict") in VERDICTS and not (label_path.parent / "design.sv").exists():
                problems.append(f"{name}: no design.sv")
            for sdc in label.get("sdc", []):
                if not (label_path.parent / sdc).exists():
                    problems.append(f"{name}: missing {sdc}")
            if disputed(label):
                problems.append(f"{name}: labellers disagree and no settling reading is recorded")
        for line in problems:
            print(line, file=sys.stderr)
        print(f"accuracy labels: {len(problems)} problem(s)")
        return 1 if problems else 0
    if not args.binary:
        parser.error("a binary is needed unless --labels-only")
    budgets_path = args.budgets
    budgets = json.loads(budgets_path.read_text()) if budgets_path else {}
    problems, rows = ([] if label_paths else ["accuracy suite contains no cases"]), []
    for label_path in label_paths:
        case_dir = label_path.parent
        name = f"{case_dir.parent.name}/{case_dir.name}"
        label = json.loads(label_path.read_text())
        found = label_problems(name, label)
        found += provenance_problems(name, label, case_dir)
        if disputed(label):
            found.append(f"{name}: labellers disagree and no settling reading is recorded")
        problems += found
        if found:
            continue
        result = run(args.binary.resolve(), case_dir, label)
        spans = {"design.sv": procedure_spans((case_dir / "design.sv").read_text())}
        row = {"case": name, "family": label["family"], "verdict": label["verdict"], "critical": bool(label.get("critical")),
               "disputed": disputed(label), "execution": result["execution"],
               **score(label, result, spans, clock_objects(case_dir, label))}
        # Bad lint cases can intentionally violate the language. Other execution failures are not hardware evidence.
        if result["execution"] == "NO REPORT":
            problems.append(f"{name}: no report (exit {result['exit']})")
            row["outcome"] = "execution error"
        elif result["execution"] != "SUCCESS" and not (label["family"] == "LNT" and label["verdict"] == "bad"):
            problems.append(f"{name}: unexpected execution status {result['execution']}")
            row["outcome"] = "execution error"
        rows.append(row)
        flag = " DISPUTED" if row["disputed"] else ""
        print(f"{name:60} {row['verdict']:8} {row['outcome']:12} {','.join(row['rules'])}{flag}")
    silent = [r["case"] for r in rows if not r["disputed"] and silent_critical(r, r["outcome"])]
    table = tabulate(rows)
    print()
    print(f"{'family':6} {'cases':>5} {'missed':>7} {'unres.bad':>9} {'false err':>9} {'false alarm':>11} {'unres.good':>10} "
          f"{'silent unk':>10} {'review/case':>11}")
    show = lambda v: "-" if v is None else f"{v:.2f}" if isinstance(v, float) else str(v)
    for family, t in table.items():
        print(f"{family:6} {t['cases']:>5} {show(t['missed_rate']):>7} {show(t['unresolved_bad_rate']):>9} "
              f"{show(t['false_error_rate']):>9} {show(t['false_alarm_rate']):>11} {show(t['unresolved_good_rate']):>10} "
              f"{t['silent_unknown']:>10} {show(t['mean_review_findings']):>11}")
    verdicts = against(table, budgets)
    over = [v for v in verdicts if v["within"] is False]
    unmeasured = sum(1 for v in verdicts if v["within"] is None)
    print(f"\nbudgets: {len(verdicts) - len(over) - unmeasured} of {len(verdicts)} within, {unmeasured} unmeasured"
          + "".join(f"\n  over: {v['family']} {v['metric']} {show(v['measured'])} > {v['limit']}" for v in over))
    print(f"silently accepted critical defects: {len(silent)}" + "".join(f"\n  {c}" for c in silent))
    if args.json:
        version = subprocess.run([str(args.binary), "--version"], capture_output=True, text=True).stdout.strip()
        measurement = {
            "schema": "sverity-accuracy/1", "tool": version,
            "budgets_sha256": hashlib.sha256(budgets_path.read_bytes()).hexdigest() if budgets_path else None,
            "families": table, "budgets": verdicts, "silent_critical": silent, "cases": rows, "problems": problems,
        }
        args.json.write_text(json.dumps(measurement, indent=1, sort_keys=True) + "\n")
    for line in problems:
        print(f"problem: {line}", file=sys.stderr)
    if args.check and (silent or problems):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
