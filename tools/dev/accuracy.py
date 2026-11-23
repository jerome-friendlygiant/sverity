#!/usr/bin/env python3
"""Scores a SVerity binary against the accuracy suite (tests/accuracy).

    accuracy.py SVERITY [--suite DIR] [--batch NAME] [--json OUT] [--budgets FILE] [--check]
    accuracy.py --labels-only [--suite DIR]    validate labels without running anything (ctest does this)

Only WARNING/ERROR/FATAL findings of the case's family count, at a labelled site: on a site line (+-1), on the header of
the procedure containing it, or naming a site's signal (an SDC clock name also stands for the object it is declared
on). CST-019, RDC-012 and RDC-013 are stated for every run and do not count. A bad case is detected, unresolved (only
an unresolved WARNING) or missed; a good case is clean, noise, unresolved or a false error; an unknown case is
disclosed or silent (a CST unknown may also be disclosed by CDC/RDC: "elsewhere"). A lint label may name its defect
class (`class`, docs/rules/LINT_WORKFLOW.md): a bad case is then detected only by a rule whose catalog `classes` hold
it, and a case of a critical class is critical. Located downstream-tap evidence can name a CDC site without making
that name a signal waiver handle. A critical bad case found only by a NOT_ANALYSED gap (it does not block the
merge) counts as not caught; an OBLIGATION, the declared undecidable question, counts as found. --batch scores one batch only. --check also fails when a budget is
exceeded or cannot be measured. --check fails on a critical case
that is missed or silent, invalid/unsettled labels, an empty suite or unexpected execution failures.

New CDC workflow labels may freeze `expected_rules` for bad and unknown cases. Such findings
must match both an expected rule and a final site signal. Protocol obligations do not detect
a bad case by themselves. Labels without `expected_rules` retain legacy scoring.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import datetime
import platform
import os
import pathlib
import re
import subprocess
import sys
import tempfile

FAMILIES = ("LNT", "CDC", "RDC", "CST")
VERDICTS = ("bad", "good", "unknown")
# Stated whatever the design does: rule coverage, unsupported power intent, reset timing that RTL can never show.
RUN_LEVEL = {"CST-019", "RDC-012", "RDC-013"}
CDC_PROTOCOL_OBLIGATIONS = {"CDC-024", "CDC-039"}
SLACK = 1                 # a procedure header and its assignment can both be reported
REPO = pathlib.Path(__file__).resolve().parents[2]


def _load_mem_guard():
    spec = importlib.util.spec_from_file_location("mem_guard", pathlib.Path(__file__).resolve().with_name("mem_guard.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


mem_guard = _load_mem_guard()


def defect_classes():
    """Class -> critical, from the lint workflow page; rule -> classes, from the catalog."""
    text = (REPO / "docs/rules/LINT_WORKFLOW.md").read_text()
    critical = {m.group(1): m.group(2) == "yes" for m in re.finditer(r"^\| `(LC\d\d)` \| .*? \| (yes|no) \|", text, re.M)}
    rules = {r["id"]: set(r.get("classes", [])) for r in json.loads((REPO / "docs/rules/catalog.json").read_text())["rules"]}
    return critical, rules


CLASS_CRITICAL, RULE_CLASSES = defect_classes()
CDC_WORKFLOW_CLASSES = {
    match.group(1)
    for match in re.finditer(r"^\| ([^|]+?) \|", (REPO / "docs/rules/CDC_WORKFLOW.md").read_text()
                            .split("## Defect classes and closure", 1)[1].split("##", 1)[0], re.M)
    if match.group(1) != "Class"
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def case_relative_path(value):
    if not isinstance(value, str) or not value:
        return False
    path = pathlib.PurePosixPath(value)
    return bool(path.parts) and not path.is_absolute() and ".." not in path.parts and path.as_posix() == value


def check_boundary_event_config(case_dir, name):
    path = case_dir / name
    if path.resolve() != case_dir.resolve() / name or not path.is_file():
        raise ValueError("boundary_event_config must be a regular file in the case without symlinks")

    def unique_object(pairs):
        obj = dict(pairs)
        if len(obj) != len(pairs):
            raise ValueError("boundary_event_config contains duplicate JSON keys")
        return obj

    config = json.loads(path.read_text(), object_pairs_hook=unique_object)
    if (not isinstance(config, dict) or set(config) != {"boundary_events"}
            or not isinstance(config["boundary_events"], list) or not config["boundary_events"]):
        raise ValueError("boundary_event_config must contain only a nonempty boundary_events array")


def selected_inputs(suite, label_paths, batch):
    """Hash every regular file in selected case directories using suite-relative names."""
    cases = {}
    for label_path in label_paths:
        directory = label_path.parent
        ancestor = directory
        while True:
            if ancestor.is_symlink():
                raise ValueError("symlinked frozen case roots are not supported")
            if ancestor == suite:
                break
            if ancestor == ancestor.parent:
                raise ValueError("frozen case is outside its suite")
            ancestor = ancestor.parent
        files = {}
        for path in sorted(directory.rglob("*")):
            if path.is_symlink():
                raise ValueError("symlinks are not supported in frozen cases")
            if path.is_file():
                files[path.relative_to(directory).as_posix()] = sha256(path)
        if "design.sv" not in files:
            raise ValueError(f"{directory.name}: missing design.sv")
        label = json.loads(label_path.read_text())
        for name in label.get("sdc", []):
            if not isinstance(name, str) or name not in files:
                raise ValueError(f"{directory.name}: missing or unsafe SDC input")
        if "boundary_event_config" in label and label["boundary_event_config"] not in files:
            raise ValueError(f"{directory.name}: missing or unsafe boundary event config")
        cases[directory.relative_to(suite / "cases").as_posix()] = files
    if not cases:
        raise ValueError("batch contains no cases")
    return {"schema": "sverity-accuracy-freeze/1", "batch": batch, "cases": cases}


def git_provenance():
    def git(*args):
        return subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, check=True).stdout
    try:
        head = git("rev-parse", "HEAD").decode().strip()
        # Include tracked staged and unstaged bytes, plus untracked names and contents.
        diff = git("diff", "--binary", "HEAD", "--")
        untracked = git("ls-files", "--others", "--exclude-standard", "-z").split(b"\0")
        h = hashlib.sha256()
        h.update(diff)
        for raw in sorted(filter(None, untracked)):
            path = REPO / os.fsdecode(raw)
            h.update(len(raw).to_bytes(8, "big")); h.update(raw)
            if path.is_symlink():
                payload = os.fsencode(os.readlink(path))
            else:
                payload = path.read_bytes()
            h.update(len(payload).to_bytes(8, "big")); h.update(payload)
        return {"head": head, "dirty": bool(diff or any(untracked)), "dirty_sha256": h.hexdigest()}
    except (OSError, subprocess.SubprocessError, UnicodeError):
        return {"head": None, "dirty": None, "dirty_sha256": None}


def analyzer_cli_flags(args):
    flags = []
    if getattr(args, "profile", None):
        flags += ["--profile", args.profile]
    for goal in getattr(args, "goal", None) or ():
        flags += ["--goal", goal]
    if getattr(args, "deck", None):
        flags += ["--deck", args.deck]
    return flags


def measurement_provenance(args, inputs, version):
    def hash_or_none(path):
        try:
            return sha256(path) if path else None
        except OSError:
            return None
    cases = inputs.get("cases", {}) if inputs else {}
    cdc_only = bool(cases) and all(name.startswith("CDC/") for name in cases)
    workflow_name = "CDC_WORKFLOW.md" if cdc_only else "LINT_WORKFLOW.md"
    cli_flags = analyzer_cli_flags(args)
    profile_meta = {
        "batch": args.batch, "check": args.check, "budgets": bool(args.budgets),
        "frozen": bool(args.verify_freeze), "first_score": args.first_score,
        "strict_fresh_labels": getattr(args, "strict_fresh_labels", False),
        "analyzer_arguments": [*cli_flags, "--top", "<top>", "--root", "<case>", "--json", "<report>",
                               "--max-console", "1", "[--config <declared boundary event config>]",
                               "--sdc", "<each declared SDC>", "<design.sv>"],
        "timeout_seconds": 300,
    }
    if getattr(args, "profile", None):
        profile_meta["analyzer_profile"] = args.profile
    if getattr(args, "goal", None):
        profile_meta["analyzer_goals"] = list(args.goal)
    if getattr(args, "deck", None):
        profile_meta["analyzer_deck"] = args.deck
    return {
        "schema": "sverity-accuracy-provenance/1",
        "binary_sha256": hash_or_none(args.binary), "binary_version": version,
        "source": git_provenance(), "selected_inputs": inputs,
        "scorer_sha256": hash_or_none(pathlib.Path(__file__)),
        "catalog_sha256": hash_or_none(REPO / "docs/rules/catalog.json"),
        "workflow_sha256": hash_or_none(REPO / "docs/rules" / workflow_name),
        "budgets_sha256": hash_or_none(args.budgets),
        "freeze_sha256": hash_or_none(args.verify_freeze),
        "profile": profile_meta,
        "context": {"utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    "platform": platform.system(), "machine": platform.machine(),
                    "python": platform.python_version()},
    }


def leaf(name):
    """`top.u.sync_q[3]` -> `sync_q`."""
    return re.sub(r"\[.*?\]", "", name).split(".")[-1]


def valid_cdc_rules(rules, *, allow_empty=False):
    return isinstance(rules, list) and (bool(rules) or allow_empty) and all(
        isinstance(rule, str) and rule in RULE_CLASSES and rule not in RUN_LEVEL
        and rule.startswith(("CDC-", "CST-", "RDC-")) for rule in rules)


def label_problems(case, label):
    found = []
    def problem(message):
        found.append(f"{case}: {message}")
    if not isinstance(label, dict):
        return [f"{case}: label must be an object"]
    for key in ("family", "top", "verdict", "sites", "labels", "author"):
        if key not in label:
            problem(f"label has no '{key}'")
    for key in ("family", "top", "verdict", "author"):
        if key in label and (not isinstance(label[key], str) or not label[key].strip()):
            problem(f"{key} must be a nonempty string")
    if label.get("family") not in FAMILIES:
        problem("family must be one of " + ", ".join(FAMILIES))
    if label.get("verdict") not in VERDICTS:
        problem("verdict must be one of " + ", ".join(VERDICTS))
    if "class" in label and (not isinstance(label["class"], str) or label["class"] not in CLASS_CRITICAL):
        problem("invalid lint defect class")
    if "class" in label and label.get("family") != "LNT":
        problem("defect classes are lint classes")
    def valid_workflow_class(value):
        return label.get("family") == "CDC" and isinstance(value, str) and value in CDC_WORKFLOW_CLASSES
    if "workflow_class" in label and not valid_workflow_class(label["workflow_class"]):
        problem("invalid CDC workflow class")
    for key in ("critical",):
        if key in label and type(label[key]) is not bool:
            problem(f"{key} must be boolean")
    for key in ("batch", "origin"):
        if key in label and (not isinstance(label[key], str) or not label[key].strip()):
            problem(f"{key} must be a nonempty string")
    # Legacy third readings use either a role string or a structured record.
    if "settled_by" in label:
        settled = label["settled_by"]
        if isinstance(settled, dict):
            if (any(not isinstance(settled.get(key), str) or not settled[key].strip()
                    for key in ("by", "verdict", "reason")) or settled.get("verdict") not in VERDICTS):
                problem("settled_by record requires by, verdict and reason strings")
        elif not isinstance(settled, str):
            problem("settled_by must be a string or third-reading record")
    def sites(value, where):
        if not isinstance(value, list):
            problem(f"{where} must be a list")
            return
        for site in value:
            if not isinstance(site, dict):
                problem(f"{where} entries must be objects")
                continue
            if "line" not in site and "signal" not in site:
                problem(f"{where} site needs a line or a signal")
            if "line" in site and (type(site["line"]) is not int or site["line"] < 1):
                problem(f"{where} line must be a positive integer")
            for key in ("file", "signal"):
                if key in site and (not isinstance(site[key], str) or not site[key].strip()):
                    problem(f"{where} {key} must be a nonempty string")
    if "sites" in label:
        sites(label["sites"], "sites")
    if label.get("verdict") in ("bad", "unknown") and not label.get("sites"):
        problem("a bad or unknown case needs sites")
    if "expected_rules" in label:
        if label.get("family") != "CDC" or label.get("verdict") not in ("bad", "unknown"):
            problem("expected_rules applies only to bad or unknown CDC workflow cases")
        rules = label["expected_rules"]
        gap_reason = label.get("detection_gap")
        declared_gap = (label.get("verdict") == "bad" and rules == []
                        and isinstance(gap_reason, str) and bool(gap_reason.strip()))
        if not valid_cdc_rules(rules, allow_empty=declared_gap):
            problem("expected_rules must list known, site-specific CDC, RDC or CST rules")
        elif rules and label.get("verdict") == "bad" and all(rule in CDC_PROTOCOL_OBLIGATIONS for rule in rules):
            problem("a bad CDC case needs an expected rule that can detect the defect")
        target_sites = label.get("sites")
        if (not isinstance(target_sites, list) or not target_sites or any(
                not isinstance(site, dict) or not site.get("signal") for site in target_sites)):
            problem("expected_rules requires a signal on every final site")
    if "detection_gap" in label and (
            label.get("family") != "CDC" or label.get("verdict") != "bad"
            or label.get("expected_rules") != [] or not isinstance(label["detection_gap"], str)
            or not label["detection_gap"].strip()):
        problem("detection_gap requires a bad CDC case, empty expected_rules, and a nonempty reason")
    entries = label.get("labels", [])
    if not isinstance(entries, list):
        problem("labels must be a list")
    else:
        for entry in entries:
            if not isinstance(entry, dict):
                problem("labeller entry must be an object")
                continue
            if entry.get("verdict") not in VERDICTS:
                problem("labeller gives no valid verdict")
            if "by" in entry and (not isinstance(entry["by"], str) or not entry["by"].strip()):
                problem("labeller by must be a nonempty string")
            if "sites" in entry:
                sites(entry["sites"], "labeller sites")
            if "class" in entry and (not isinstance(entry["class"], str) or entry["class"] not in CLASS_CRITICAL):
                problem("invalid labeller class")
            if "workflow_class" in entry and not valid_workflow_class(entry["workflow_class"]):
                problem("invalid labeller CDC workflow class")
            if "critical" in entry and type(entry["critical"]) is not bool:
                problem("labeller critical must be boolean")
            if "expected_rules" in entry and (label.get("family") != "CDC"
                    or entry.get("verdict") not in ("bad", "unknown") or not valid_cdc_rules(
                        entry["expected_rules"], allow_empty=True)):
                problem("labeller expected_rules requires a bad or unknown CDC reading and site-specific rules")
    if "sdc" in label and (not isinstance(label["sdc"], list) or any(
            not isinstance(x, str) or not x or pathlib.PurePosixPath(x).is_absolute()
            or ".." in pathlib.PurePosixPath(x).parts for x in label["sdc"])):
        problem("sdc must be a list of relative paths within the case")
    if "boundary_event_config" in label and (
            label.get("family") != "CDC" or not case_relative_path(label["boundary_event_config"])):
        problem("boundary_event_config must be a relative path within a CDC case")
    adjudication = label.get("adjudication")
    if "adjudication" in label:
        if not isinstance(adjudication, dict) or any(
                not isinstance(adjudication.get(k), str) or not adjudication[k].strip() for k in ("by", "reason", "record")):
            problem("adjudication requires by, reason and record strings")
        elif (not isinstance(adjudication.get("was"), dict) or adjudication["was"].get("verdict") not in VERDICTS
              or adjudication["was"].get("class") not in CLASS_CRITICAL or type(adjudication["was"].get("critical")) is not bool):
            problem("adjudication.was requires a verdict, a class and a boolean critical")
    if "provenance" in label and not isinstance(label["provenance"], dict):
        problem("provenance must be an object")
    elif isinstance(label.get("provenance"), dict) and any(not isinstance(v, str) for v in label["provenance"].values()):
        problem("provenance values must be strings")
    return found


def adjudicated(label):
    """A post-score adjudication that names its record decides the final label, superseding readings and settlement."""
    adj = label.get("adjudication")
    return isinstance(adj, dict) and all(isinstance(adj.get(k), str) and adj[k].strip() for k in ("by", "record"))


def reading_triple(reading):
    """The parts of a reading an adjudication moves from or to: verdict, class, critical."""
    return {k: reading.get(k) for k in ("verdict", "class", "critical")}


def settled_reading(label):
    """The reading the case settled on before any adjudication: a settlement that names who settled it, else the readers'
    agreement on verdict, class and critical; None when neither exists (a settlement without `by` is not a base)."""
    settlement = label.get("settlement")
    if isinstance(settlement, dict):
        by = settlement.get("by")
        if not isinstance(by, str) or not by.strip():
            return None
        readers = {person(r.get("by", "")) for r in label.get("labels", []) if isinstance(r, dict)}
        return None if any(names_id(by, reader) for reader in readers) else reading_triple(settlement)   # a reader cannot settle his own dispute
    readings = [reading_triple(r) for r in label.get("labels", []) if isinstance(r, dict)]
    return readings[0] if readings and all(json.dumps(r, sort_keys=True) == json.dumps(readings[0], sort_keys=True) for r in readings) else None


def person(text):
    """Who a reading or adjudication is by: the role id, cut at the first parenthesis (the model) or comma (a note), ignoring case."""
    return re.split(r"[(,]", str(text), maxsplit=1)[0].strip().casefold()


def readings_digest(label):
    """A hash of the readings and settlement as the case stores them, with the top and the author: the record pins it, so an edit of a
    reader's class or sites, of the settlement, of the top module or of who wrote the case, behind an adjudication is caught."""
    text = json.dumps({"labels": label.get("labels", []), "settlement": label.get("settlement"), "top": label.get("top"),
                       "author": label.get("author")}, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(text.encode()).hexdigest()


def names_id(text, ident):
    """Whether `text` names the id `ident` as a whole id anywhere (case-insensitive). Separators are not part of an id: the id is
    its letter and digit runs, in order, with anything or nothing between them ("Reader1", "Reader-1", "Reader\u20131" and
    "reader_1" are one reader; "Reader 12" is another)."""
    parts = [re.escape(x) for x in re.findall(r"[^\W\d_]+|\d+", str(ident))]
    return bool(parts) and re.search(r"(?<![^\W_])" + r"[\W_]*".join(parts) + r"(?![^\W_])", str(text), re.IGNORECASE) is not None


def same(a, b):
    """Equality that does not take True for 1 inside nested values."""
    return json.dumps(a, sort_keys=True) == json.dumps(b, sort_keys=True)


def adjudication_record_problems(name, case_dir, label, suite):
    """The record is a repo path (the suite is <repo>/tests/accuracy), not a label, and lists this case once, keyed `family/case`. Its entry
    states what the case was settled at (`was`: verdict, class, critical) and the final reading (`now`: verdict, class, critical, sites);
    the label must equal both, and the two must differ. The adjudicator is none of the people who authored, read or settled the case."""
    if not adjudicated(label):
        return []
    adjudication = label["adjudication"]
    record = adjudication["record"]
    path = pathlib.PurePosixPath(record)
    if path.is_absolute() or ".." in path.parts:
        return [f"{name}: adjudication record must be a relative repo path"]
    if path.name == "label.json":
        return [f"{name}: adjudication record {record} is a label, not a record"]
    key = f"{label.get('family')}/{case_dir.name}"
    try:
        data = json.loads((suite.parent.parent / record).read_text())
        listed = [c for c in data.get("cases", []) if isinstance(c, dict) and c.get("case") == key]
    except (OSError, UnicodeError, ValueError, AttributeError) as error:
        return [f"{name}: cannot read adjudication record {record}: {error}"]
    if not listed:
        return [f"{name}: adjudication record {record} does not list this case ({key})"]
    if len(listed) > 1:
        return [f"{name}: adjudication record {record} lists this case ({key}) {len(listed)} times"]
    entry = listed[0]
    found = []
    if data.get("batch") != label.get("batch"):
        found.append(f"{name}: adjudication record is for batch {data.get('batch')!r}, the label is in {label.get('batch')!r}")
    was, now = entry.get("was"), entry.get("now")
    if not isinstance(was, dict) or not isinstance(now, dict):
        return found + [f"{name}: adjudication record entry needs `was` and `now` readings (verdict, class, critical; now also sites)"]
    label_was = reading_triple(adjudication.get("was", {}))
    if not same(was, label_was):
        found.append(f"{name}: adjudication record says the case was {was!r}, the label says {label_was!r}")
    final = {**reading_triple(label), "sites": label.get("sites", [])}
    if not same(now, final):
        found.append(f"{name}: adjudication record says the case is now {now!r}, the label is {final!r}")
    if same({k: now.get(k) for k in ("verdict", "class", "critical")}, was):
        found.append(f"{name}: adjudication record moves the case from {was!r} to the same reading")
    settled = settled_reading(label)
    if settled is None:
        found.append(f"{name}: adjudication needs the readings settled (an agreement or a settlement that names who settled it) for it to move from")
    elif not same(label_was, settled):
        found.append(f"{name}: adjudication.was is {label_was!r}, the readings settled on {settled!r}")
    others = {person(label.get("author", ""))}
    others |= {person(r.get("by", "")) for r in label.get("labels", []) if isinstance(r, dict)}
    if isinstance(label.get("settlement"), dict):
        others.add(person(label["settlement"].get("by", "")))
    if any(names_id(adjudication["by"], other) for other in others):
        found.append(f"{name}: the adjudicator must not be the author, a reader or the settler")
    decided_by = entry.get("decided_by")
    if not isinstance(decided_by, str) or not decided_by.strip():
        found.append(f"{name}: adjudication record entry needs decided_by")
    elif not names_id(adjudication["by"], decided_by):
        found.append(f"{name}: adjudication.by {adjudication['by']!r} does not name the record's decided_by {decided_by!r}")
    if adjudication["reason"] != entry.get("reason"):
        found.append(f"{name}: adjudication.reason differs from the reason the record states")
    if entry.get("readings_sha256") != readings_digest(label):
        found.append(f"{name}: adjudication record readings_sha256 does not match the readings and settlement stored in the case")
    return found


def strict_label_problems(case, label):
    """Check declared readings, not their actual independence or blindness."""
    errors = []
    def error(message):
        errors.append(f"{case}: strict fresh: {message}")
    class_field = {"LNT": "class", "CDC": "workflow_class"}.get(label.get("family"))
    if not class_field or class_field not in label or "critical" not in label:
        error("a LNT class or CDC workflow_class and critical are required")
        return errors
    readings = label.get("labels", [])
    if len(readings) != 2:
        error("exactly two initial readings are required")
        return errors
    author = label["author"].strip()
    ids = [r.get("by", "").strip() for r in readings]
    if not all(ids) or len({author, *ids}) != 3:
        error("author and two reader role IDs must be distinct")
    keys = ("verdict", class_field, "sites", "critical")
    settlement = label.get("settlement")
    if ("expected_rules" in label or any("expected_rules" in r for r in readings)
            or isinstance(settlement, dict) and "expected_rules" in settlement):
        if label.get("verdict") != "good" and not valid_cdc_rules(
                label.get("expected_rules"), allow_empty=bool(label.get("detection_gap"))):
            error("the final label must retain valid expected_rules when the readings declare them")
            return errors
        keys += ("expected_rules",)
    if any(any(k not in r and not (k == "expected_rules" and r.get("verdict") == "good")
               for k in keys) for r in readings):
        error("each reading requires " + ", ".join(keys))
        return errors
    if "expected_rules" in keys and any(r.get("verdict") != "good" and not valid_cdc_rules(
            r["expected_rules"], allow_empty=True) for r in readings):
        error("each reading requires valid expected_rules")
        return errors

    def opinion(reading):
        result = {k: reading.get(k) for k in keys}
        if "expected_rules" in result and valid_cdc_rules(result["expected_rules"], allow_empty=True):
            result["expected_rules"] = sorted(set(result["expected_rules"]))
        return result

    target = opinion(label)
    opinions = [opinion(r) for r in readings]
    if "settlement" in label:
        if not isinstance(settlement, dict):
            error("settlement must be an object")
            return errors
        # Use the same field types as initial readings before comparing values:
        # Python otherwise treats True == 1, including inside nested sites.
        typed = label_problems(case, {**label, "labels": [settlement]})
        if typed:
            errors.extend(f"{case}: strict fresh settlement: {item}" for item in typed)
            return errors
    if adjudicated(label) or opinions[0] == opinions[1] == target:
        return errors
    if (not isinstance(settlement, dict) or not isinstance(settlement.get("by"), str)
            or not settlement["by"].strip() or settlement["by"].strip() in {author, *ids}
            or opinion(settlement) != target):
        error("disagreement requires an independent third settlement matching the final label")
    return errors


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
    if label.get("settled_by") or adjudicated(label):
        return False
    # A typed third reading (strict fresh labels) settles the case too; strict validation checks it matches the label.
    settlement = label.get("settlement")
    if isinstance(settlement, dict) and isinstance(settlement.get("by"), str) and settlement["by"].strip() \
            and settlement.get("verdict") == label.get("verdict"):
        return False
    verdicts = {entry.get("verdict") for entry in label.get("labels", [])}
    return len(label.get("labels", [])) < 2 or len(verdicts) > 1



def validate_diagnostic(diagnostic):
    """Validate the report fields consumed by site matching and outcome scoring."""
    if not isinstance(diagnostic, dict) or not isinstance(diagnostic.get("rule_id"), str):
        raise ValueError("diagnostic requires a string rule_id")
    if diagnostic.get("severity") not in ("INFO", "WARNING", "ERROR", "FATAL"):
        raise ValueError("invalid diagnostic severity")
    for key in ("message", "origin"):
        if key in diagnostic and not isinstance(diagnostic[key], str):
            raise ValueError(f"diagnostic {key} must be a string")
    if "confidence" in diagnostic and diagnostic["confidence"] not in (
            "established", "heuristic", "unresolved", "informational"):
        raise ValueError("invalid diagnostic confidence")
    signals = diagnostic.get("signals", [])
    if not isinstance(signals, list) or any(not isinstance(name, str) for name in signals):
        raise ValueError("diagnostic signals must be a list of strings")
    crossing = diagnostic.get("crossing")
    if crossing is not None:
        if not isinstance(crossing, dict):
            raise ValueError("diagnostic crossing must be an object")
        for role in ("launch", "capture"):
            endpoint = crossing.get(role)
            if endpoint is not None and (not isinstance(endpoint, dict)
                    or not isinstance(endpoint.get("signal", ""), str)):
                raise ValueError(f"crossing {role} must be an object with an optional signal string")
    evidence = diagnostic.get("evidence", [])
    if not isinstance(evidence, list) or any(not isinstance(item, dict) for item in evidence):
        raise ValueError("diagnostic evidence must be a list of objects")
    for location in [diagnostic.get("location")] + [item.get("location") for item in evidence]:
        if location is None:
            continue
        if not isinstance(location, dict):
            raise ValueError("diagnostic location must be an object")
        if "file" in location and not isinstance(location["file"], str):
            raise ValueError("diagnostic location file must be a string")
        for key in ("line", "column"):
            if key in location and (type(location[key]) is not int or location[key] < 0):
                raise ValueError(f"diagnostic location {key} must be a nonnegative integer")


def run(binary, case_dir, label, cli_flags=()):
    result = {"execution": "NOT RUN", "exit": None, "coverage": None,
              "timeout": False, "reason": None, "diagnostics": []}
    with tempfile.TemporaryDirectory() as scratch:
        report = pathlib.Path(scratch) / "report.json"
        args = [str(binary), *cli_flags, "--top", label["top"], "--root", str(case_dir), "--json", str(report), "--max-console", "1"]
        if "boundary_event_config" in label:
            args += ["--config", str(case_dir / label["boundary_event_config"])]
        for sdc in label.get("sdc", []):
            args += ["--sdc", str(case_dir / sdc)]
        args.append(str(case_dir / "design.sv"))
        guarded_args, unit = mem_guard.build_command(args, profile="accuracy", label=case_dir.name, exec_mode=True)
        try:
            done = subprocess.run(guarded_args, capture_output=True, text=True, timeout=300)
        except subprocess.TimeoutExpired:
            mem_guard.stop_unit(unit)
            return {**result, "execution": "TIMEOUT", "timeout": True, "reason": "command timed out"}
        except OSError as error:
            return {**result, "execution": "LAUNCH ERROR", "reason": str(error)}
        oom, detail = mem_guard.inspect_oom(unit, done.stderr, done.returncode,
                                            profile="accuracy", label=case_dir.name)
        if oom:
            return {**result, "exit": 137, "execution": "MEMORY LIMIT", "reason": detail}
        result["exit"] = done.returncode
        if done.returncode < 0:
            return {**result, "execution": "CRASH", "reason": f"terminated by signal {-done.returncode}"}
        if not report.exists():
            return {**result, "execution": "NO REPORT", "reason": "report missing"}
        try:
            data = json.loads(report.read_text())
            summary = data["summary"]
            diagnostics = data["diagnostics"]
            if (data["schema"] != "sverity-report/1" or not isinstance(summary, dict)
                    or summary["execution_status"] not in ("SUCCESS", "ERROR")
                    or summary["analysis_coverage"] not in ("COMPLETE", "INCOMPLETE")
                    or not isinstance(diagnostics, list)):
                raise ValueError("invalid report fields")
            for diagnostic in diagnostics:
                validate_diagnostic(diagnostic)
        except (OSError, UnicodeError, ValueError, KeyError, TypeError) as error:
            return {**result, "execution": "INVALID REPORT", "reason": str(error)}
        result.update(execution=summary["execution_status"], coverage=summary["analysis_coverage"],
                      diagnostics=diagnostics)
        # The CLI uses a strict 1/2/4/8 bitmask. Signal exits and unknown bits are failures.
        if done.returncode > 15 or bool(done.returncode & 8) != (result["execution"] == "ERROR"):
            result["reason"] = "exit code inconsistent with execution status"
        return result


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
    evidence = diagnostic.get("evidence", [])
    places = [diagnostic.get("location", {})] + [e.get("location", {}) for e in evidence]
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
        if "signal" in site:
            wanted_names = {site["signal"]} | aliases.get(site["signal"], set())
            if wanted_names & names:
                return True
            for item in evidence:
                if item.get("kind") != "downstream_tap" or not isinstance(item.get("text"), str):
                    continue
                location = item.get("location") or {}
                if pathlib.PurePath(location.get("file", "")).name == wanted and leaf(item["text"]) in wanted_names:
                    return True
    return False


def sites_of(label):
    """The sites every labeller gave: several sites mean any of them."""
    sites = list(label.get("sites", []))
    if adjudicated(label):
        return sites   # the record's `now.sites`, which the label must equal: the readers' own sites do not widen an adjudicated case
    for entry in label.get("labels", []):
        sites += [site for site in entry.get("sites", []) if site not in sites]
    return sites


def cdc_signal_match(name, signal):
    def split_index(value):
        match = re.fullmatch(r"(.*?)(\[-?\d+\])?", value)
        return match.groups() if match else (value, None)

    base, bit = split_index(name)
    wanted, wanted_bit = split_index(signal)
    return ((base == wanted or base.endswith("." + wanted))
            and (bit is None or wanted_bit is None or bit == wanted_bit))


def cdc_defect_match(diagnostic, label):
    """Require a frozen rule and signal; a nearby line alone is insufficient."""
    if diagnostic["rule_id"] not in label["expected_rules"]:
        return False
    names = list(diagnostic.get("signals", []))
    crossing = diagnostic.get("crossing") or {}
    for role in ("launch", "capture"):
        signal = (crossing.get(role) or {}).get("signal")
        if signal:
            names.append(signal)
    for site in label["sites"]:
        signal = site["signal"]
        if any(cdc_signal_match(name, signal) for name in names):
            return True
        for witness in diagnostic.get("evidence", []):
            location = witness.get("location") or {}
            file = pathlib.PurePath(location.get("file", ""))
            wanted_file = site.get("file", "design.sv")
            # The CLI roots site_signal locations at the case directory.
            kind = witness.get("kind")
            if ((kind == "site_signal" and location.get("line", 0) > 0 and file == pathlib.PurePath(wanted_file))
                    or (kind == "downstream_tap" and file.name == wanted_file)):
                name = witness.get("text", "")
                if isinstance(name, str) and cdc_signal_match(name, signal):
                    return True
    return False


def cdc_nongating(diagnostic):
    return (diagnostic.get("disposition") in ("NOT_ANALYSED", "OBLIGATION", "WAIVED", "RESOLVED")
            or diagnostic["rule_id"] in CDC_PROTOCOL_OBLIGATIONS)


def score(label, result, spans, aliases):
    family = label["family"]
    defect_match = "expected_rules" in label
    if not defect_match:
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
    if defect_match:
        hits = [d for d in raised if cdc_defect_match(d, label)]
        if verdict == "bad":
            detected = any(not unresolved(d) and not cdc_nongating(d) for d in hits)
            outcome = "detected" if detected else "unresolved" if hits else "missed"
        else:
            hits = [d for d in hits if d.get("disposition") not in ("RESOLVED", "WAIVED")]
            outcome = "disclosed" if hits else "silent"
    elif verdict == "bad":
        hits = [d for d in mine if at_site(d, label["sites"], spans, aliases)
                and ("class" not in label or label["class"] in RULE_CLASSES.get(d["rule_id"], ()))]
        outcome = "detected" if any(not unresolved(d) for d in hits) else "unresolved" if hits else "missed"
    elif verdict == "good":
        outcome = ("false error" if any(d["severity"] in ("ERROR", "FATAL") for d in mine)
                   else "noise" if any(not unresolved(d) for d in mine)
                   else "unresolved" if mine else "clean")
        hits = mine
    else:
        hits = [d for d in mine if at_site(d, label["sites"], spans, aliases)]
        outcome = "disclosed" if hits else "elsewhere" if elsewhere else "silent"
    # A not-analysed gap does not block the merge, so a critical bad case it alone points at is not caught. A declared
    # obligation is the exception: the question is undecidable by lint and the finding names it.
    gap_only = verdict == "bad" and bool(hits) and (
        all(cdc_nongating(d) for d in hits) if defect_match
        else outcome == "unresolved" and all(d.get("disposition") == "NOT_ANALYSED" for d in hits))
    return {
        "outcome": outcome,
        "rules": sorted({d["rule_id"] for d in (hits or (elsewhere if outcome == "elsewhere" else []))}),
        "review_findings": len(raised),
        "unresolved_findings": sum(1 for d in raised if unresolved(d)),
        "not_analysed_findings": sum(1 for d in raised if d.get("disposition") == "NOT_ANALYSED"),
        "nongating_only": gap_only,
    }


def silent_critical(label, outcome):
    return bool(label.get("critical")) and (outcome in ("missed", "silent") or bool(label.get("nongating_only")))


def undetected_critical_classes(rows, contracts=None):
    """Critical classes that have labelled bad cases but none of them detected; None when no case names a class.
    A class whose contract (budgets.json `class_contracts`, from the workflow's class table) is disclosure is covered instead
    when it has a case that is not good (bad, or unknown) and every such case is disclosed (a bad case found or Unresolved or an
    obligation, an unknown one disclosed): none missed, none silently passed. Good cases are judged by the false-alarm budgets."""
    lint = [r for r in rows if r.get("family") == "LNT" and not r["disputed"]]
    if not lint:
        return None
    contracts = contracts or {}

    def covered(k):
        bad = [r for r in lint if r.get("class") == k and r["verdict"] == "bad"]
        if contracts.get(k) == "disclosure":
            open_cases = [r for r in lint if r.get("class") == k and r["verdict"] in ("bad", "unknown")]
            return bool(open_cases) and all(r["outcome"] in ("detected", "unresolved", "disclosed") for r in open_cases)
        return any(r["outcome"] == "detected" for r in bad)
    return sum(1 for k, critical in CLASS_CRITICAL.items() if critical and not covered(k))


def tabulate(rows, contracts=None):
    table = {}
    for family in FAMILIES:
        mine = [r for r in rows if r["family"] == family and not r["disputed"]]
        counts = {}
        for r in mine:
            counts.setdefault(r["verdict"], {}).setdefault(r["outcome"], 0)
            counts[r["verdict"]][r["outcome"]] += 1
        bad, good, unknown = (sum(n for outcome, n in counts.get(v, {}).items() if outcome != "execution error") for v in VERDICTS)
        rate = lambda n, d: round(n / d, 3) if d else None
        c = lambda v, o: counts.get(v, {}).get(o, 0)
        table[family] = {
            "cases": len(mine), "adjudicated": sum(1 for r in mine if r.get("adjudicated")), "execution_errors": sum(r["outcome"] == "execution error" for r in mine), "disputed": sum(1 for r in rows if r["family"] == family and r["disputed"]),
            "counts": counts,
            "missed_rate": rate(c("bad", "missed"), bad),
            "unresolved_bad_rate": rate(c("bad", "unresolved"), bad),
            "false_error_rate": rate(c("good", "false error"), good),
            "false_alarm_rate": rate(c("good", "false error") + c("good", "noise"), good),
            "unresolved_good_rate": rate(c("good", "unresolved"), good),
            "review_good_rate": rate(good - c("good", "clean"), good),
            "not_analysed_good_rate": rate(sum(1 for r in mine if r["verdict"] == "good" and r.get("not_analysed_findings")), good),
            "critical_nongating_only": sum(1 for r in mine if r["verdict"] == "bad" and r.get("critical") and r.get("nongating_only")),
            "critical_classes_undetected": undetected_critical_classes(mine, contracts),
            "critical_classes_missing_bad": (sorted(k for k, critical in CLASS_CRITICAL.items() if critical
                and not any(r.get("class") == k and r["verdict"] == "bad" for r in mine))
                if family == "LNT" and mine else None),
            "silent_unknown": c("unknown", "silent") + c("unknown", "elsewhere"),
            "mean_review_findings": rate(sum(r["review_findings"] for r in mine if r["outcome"] != "execution error"),
                                         sum(r["outcome"] != "execution error" for r in mine)),
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


def load_cases(suite, batch, strict):
    cases = []
    for directory in sorted(p for p in (suite / "cases").glob("*/*") if p.is_dir()):
        path = directory / "label.json"
        name = f"{path.parent.parent.name}/{path.parent.name}"
        try:
            label = json.loads(path.read_text())
        except (OSError, UnicodeError, ValueError) as error:
            cases.append((path, {}, [f"{name}: cannot read label: {error}"], None))
            continue
        if not isinstance(label, dict):
            cases.append((path, {}, [f"{name}: label must be an object; batch membership unknown"], None))
            continue
        membership = label.get("batch")
        if batch and isinstance(membership, str) and membership != batch:
            continue
        if batch and "batch" not in label:
            continue  # legacy unbatched labels are outside a named batch
        found = label_problems(name, label)
        if not found:
            found += adjudication_record_problems(name, directory, label, suite)
        if batch and not isinstance(membership, str):
            found.append(f"{name}: batch membership unknown")
        if not found:
            if strict:
                found += strict_label_problems(name, label)
            if disputed(label) and not (strict and label.get("settlement") and not found):
                found.append(f"{name}: labellers disagree and no settling reading is recorded")
        try:
            source = (path.parent / "design.sv").read_text()
        except (OSError, UnicodeError) as error:
            source = None
            found.append(f"{name}: cannot read design.sv: {error}" + (" (no design.sv)" if isinstance(error, FileNotFoundError) else ""))
        if not label_problems(name, label):
            if source is not None:
                try:
                    found += provenance_problems(name, label, path.parent)
                except (OSError, UnicodeError) as error:
                    found.append(f"{name}: cannot read provenance source: {error}")
            for sdc in label.get("sdc", []):
                try:
                    (path.parent / sdc).read_text()
                except (OSError, UnicodeError) as error:
                    found.append(f"{name}: cannot read SDC {sdc}: {error}")
            if "boundary_event_config" in label:
                try:
                    check_boundary_event_config(path.parent, label["boundary_event_config"])
                except (OSError, UnicodeError, ValueError, RuntimeError) as error:
                    found.append(f"{name}: {error}")
        cases.append((path, label, found, source))
    return cases


CONTRACTS = ("disclosure",)


def class_contracts(budgets):
    """Class -> contract, from the budgets file: a class the workflow says is disclosed, not decided."""
    return {k: v["contract"] for k, v in budgets.get("class_contracts", {}).items()}


def read_budgets(path):
    if not path:
        return {}
    data = json.loads(path.read_text())
    if not isinstance(data, dict) or not isinstance(data.get("families", {}), dict):
        raise ValueError("budgets must contain a families object")
    contracts = data.get("class_contracts", {})
    if not isinstance(contracts, dict):
        raise ValueError("class_contracts must be an object")
    for k, entry in contracts.items():
        if (k not in CLASS_CRITICAL or not isinstance(entry, dict) or entry.get("contract") not in CONTRACTS or not entry.get("reason")
                or not isinstance(entry.get("approved_by"), str) or not entry["approved_by"].strip()):
            raise ValueError(f"invalid class contract for {k}: a known class, a contract of {CONTRACTS}, a reason and approved_by are required")
    allowed = {"missed_rate", "unresolved_bad_rate", "false_error_rate", "false_alarm_rate",
               "unresolved_good_rate", "review_good_rate", "critical_classes_undetected",
               "not_analysed_good_rate", "critical_nongating_only",
               "silent_unknown", "mean_review_findings", "execution_errors", "cases", "disputed"}
    for family, limits in data.get("families", {}).items():
        if family not in FAMILIES or not isinstance(limits, dict):
            raise ValueError("invalid budget family or limits")
        for key, value in limits.items():
            try:
                finite = type(value) in (int, float) and math.isfinite(value)
            except OverflowError:
                finite = False
            if (not key.startswith("max_") or key[4:] not in allowed
                    or not finite or value < 0):
                raise ValueError("invalid budget metric or limit")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", type=pathlib.Path, nargs="?")
    parser.add_argument("--suite", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[2] / "tests/accuracy")
    parser.add_argument("--json", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--labels-only", action="store_true")
    parser.add_argument("--batch", help="score only cases whose label names this batch")
    parser.add_argument("--budgets", type=pathlib.Path, help='limits to compare with: {"families": {"CDC": {"max_missed_rate": 0.05}}}')
    parser.add_argument("--freeze-batch", type=pathlib.Path, help="create a new frozen input manifest without launching the analyzer")
    parser.add_argument("--verify-freeze", type=pathlib.Path, help="verify a frozen batch before scoring")
    parser.add_argument("--first-score", action="store_true", help="require a verified freeze and exclusively create the JSON output")
    parser.add_argument("--strict-fresh-labels", action="store_true", help="enforce declared fresh-batch reading metadata")
    parser.add_argument("--profile", choices=("generic", "asic", "fpga"), help="pass --profile <name> to the analyzer")
    parser.add_argument("--goal", action="append", choices=("lint", "cdc", "rdc"), help="pass --goal <name> to the analyzer (repeatable)")
    parser.add_argument("--deck", choices=("core", "domain", "1.0", "lint", "cdc"), help="pass --deck <name> to the analyzer")
    args = parser.parse_args()
    if (args.freeze_batch or args.verify_freeze) and not args.batch:
        parser.error("freeze operations require --batch")
    if args.freeze_batch and (args.verify_freeze or args.binary or args.labels_only or args.json or args.first_score):
        parser.error("--freeze-batch only creates a manifest")
    if args.first_score and (not args.verify_freeze or not args.json or args.labels_only):
        parser.error("--first-score requires --verify-freeze and --json")
    if args.strict_fresh_labels and not args.batch:
        parser.error("--strict-fresh-labels requires --batch")
    if not args.binary and not args.labels_only and not args.freeze_batch:
        parser.error("a binary is needed unless --labels-only or --freeze-batch")
    try:
        budgets = read_budgets(args.budgets)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"budget error: {error}", file=sys.stderr)
        return 1
    cases = load_cases(args.suite, args.batch, args.strict_fresh_labels)
    label_paths = [p for p, _, _, _ in cases]
    preflight_problems = [problem for _, _, found, _ in cases for problem in found]
    if (args.freeze_batch or args.verify_freeze or args.strict_fresh_labels) and preflight_problems:
        for problem in preflight_problems:
            print(f"input error: {problem}", file=sys.stderr)
        return 1
    inputs = None
    try:
        if label_paths and (args.freeze_batch or args.verify_freeze or (args.json and not args.labels_only)):
            inputs = selected_inputs(args.suite, label_paths, args.batch)
        if args.freeze_batch:
            if inputs is None:
                raise ValueError("batch contains no cases")
            with args.freeze_batch.open("x") as out:
                json.dump(inputs, out, indent=1, sort_keys=True)
                out.write("\n")
            print("frozen batch manifest created")
            return 0
        if args.verify_freeze:
            frozen = json.loads(args.verify_freeze.read_text())
            if inputs is None or frozen != inputs:
                raise ValueError("frozen batch membership or input hashes differ")
    except (OSError, ValueError, TypeError) as error:
        if args.freeze_batch or args.verify_freeze:
            print(f"freeze/input error: {error}", file=sys.stderr)
            return 1
        # Ordinary regression retains per-case accounting; an unavailable hash
        # inventory is context, not a reason to discard the rest of the suite.
        inputs = {"schema": "sverity-accuracy-freeze/1", "error": str(error)}
    # Reserve the output before any analyzer invocation; interrupted runs retain the reservation.
    output = None
    if args.first_score:
        try:
            output = args.json.open("x")
        except OSError as error:
            print(f"first-score output error: {error}", file=sys.stderr)
            return 1
    if args.labels_only:   # held out: the suite's results are measured on purpose, not on every test run
        problems = [] if label_paths else ["accuracy suite contains no cases"]
        problems += preflight_problems
        for line in problems:
            print(line, file=sys.stderr)
        print(f"accuracy labels: {len(problems)} problem(s)")
        return 1 if problems else 0
    provenance = None
    version = "unavailable"
    if args.json:
        try:
            version = subprocess.run([str(args.binary), "--version"], capture_output=True, text=True, timeout=10).stdout.strip()
        except (OSError, subprocess.TimeoutExpired):
            pass
        provenance = measurement_provenance(args, inputs, version)
    budgets_path = args.budgets
    cli_flags = analyzer_cli_flags(args)
    problems, rows = ([] if label_paths else ["accuracy suite contains no cases"]), []
    for label_path, label, found, source in cases:
        case_dir = label_path.parent
        name = f"{case_dir.parent.name}/{case_dir.name}"
        problems += found
        valid = not label_problems(name, label)
        critical = (bool(label.get("critical")) or (label.get("verdict") != "good"
                    and CLASS_CRITICAL.get(label.get("class"), False))) if valid else False
        is_disputed = disputed(label) if valid else False
        if args.strict_fresh_labels and label.get("settlement") and not found:
            is_disputed = False
        row = {"case": name, "family": label.get("family") if valid else None, "verdict": label.get("verdict") if valid else None,
               "batch_membership": "unknown" if args.batch and not isinstance(label.get("batch"), str) else "selected",
               "critical": critical, "class": label.get("class") if valid else None, "disputed": is_disputed,
               "adjudicated": bool(valid and adjudicated(label)),
               "execution": "NOT RUN", "exit": None, "coverage": None, "timeout": False,
               "reason": "; ".join(found) if found else None, "diagnostics": [],
               "outcome": "execution error", "rules": [], "review_findings": 0, "unresolved_findings": 0}
        if valid and "workflow_class" in label:
            row["workflow_class"] = label["workflow_class"]
        if valid and "expected_rules" in label:
            row["expected_rules"] = label["expected_rules"]
        if valid and "detection_gap" in label:
            row["detection_gap"] = label["detection_gap"]
        if valid and "boundary_event_config" in label:
            row["boundary_event_config"] = label["boundary_event_config"]
        if not found:
            result = run(args.binary.resolve(), case_dir, label, cli_flags=cli_flags)
            row.update({k: result[k] for k in ("execution", "exit", "coverage", "timeout", "reason", "diagnostics")})
            spans = {"design.sv": procedure_spans(source)}
            # A frontend error at the labelled site distinguishes expected LC01 rejection
            # from an internal failure, which can itself emit LNT-001 FATAL.
            expected = (label["family"] == "LNT" and label["verdict"] in ("bad", "unknown") and label.get("class") == "LC01"
                        and (result["execution"] == "ERROR" or result.get("coverage") == "INCOMPLETE")
                        and any(d.get("origin") == "slang" and d["severity"] in ("ERROR", "FATAL")
                                and "LC01" in RULE_CLASSES.get(d["rule_id"], ())
                                and at_site(d, sites_of(label), spans, {}) for d in result["diagnostics"])
                        and not any("internal failure" in d.get("message", "").lower() for d in result["diagnostics"]))
            if not result["reason"] and (result["execution"] == "SUCCESS" or expected):
                try:
                    aliases = clock_objects(case_dir, label)
                except (OSError, UnicodeError) as error:
                    row["reason"] = f"cannot read SDC: {error}"
                    problems.append(f"{name}: {row['reason']}")
                else:
                    row.update(score(label, result, spans, aliases))
                    row["expected_rejection"] = expected
            else:
                row["reason"] = result["reason"] or f"unexpected execution status {result['execution']}"
                problems.append(f"{name}: {row['reason']}")
        rows.append(row)
        flag = " DISPUTED" if row["disputed"] else ""
        print(f"{name:60} {str(row['verdict'] or 'invalid'):8} {row['outcome']:12} {','.join(row['rules'])}{flag}")
    silent = [r["case"] for r in rows if not r["disputed"] and silent_critical(r, r["outcome"])]
    table = tabulate(rows, class_contracts(budgets))
    print()
    print(f"{'family':6} {'cases':>5} {'missed':>7} {'unres.bad':>9} {'false err':>9} {'false alarm':>11} {'unres.good':>10} "
          f"{'silent unk':>10} {'review/case':>11}")
    show = lambda v: "-" if v is None else f"{v:.2f}" if isinstance(v, float) else str(v)
    for family, t in table.items():
        print(f"{family:6} {t['cases']:>5} {show(t['missed_rate']):>7} {show(t['unresolved_bad_rate']):>9} "
              f"{show(t['false_error_rate']):>9} {show(t['false_alarm_rate']):>11} {show(t['unresolved_good_rate']):>10} "
              f"{t['silent_unknown']:>10} {show(t['mean_review_findings']):>11}")
    adjudicated_per_family = {f: t["adjudicated"] for f, t in table.items() if t["adjudicated"]}
    if adjudicated_per_family:
        print("\nadjudicated labels (scored at their adjudicated verdict): " + ", ".join(f"{f} {n}" for f, n in adjudicated_per_family.items()))
    classes = {}
    for r in rows:
        if r["class"]:
            classes.setdefault(r["class"], {}).setdefault(f"{r['verdict']} {r['outcome']}", 0)
            classes[r["class"]][f"{r['verdict']} {r['outcome']}"] += 1
    if classes:
        print("\nlint defect classes (verdict outcome: cases)")
        for k in sorted(classes):
            print(f"  {k}  " + ", ".join(f"{o}: {n}" for o, n in sorted(classes[k].items())))
    verdicts = against(table, budgets)
    over = [v for v in verdicts if v["within"] is False]
    unmeasured = sum(1 for v in verdicts if v["within"] is None)
    print(f"\nbudgets: {len(verdicts) - len(over) - unmeasured} of {len(verdicts)} within, {unmeasured} unmeasured"
          + "".join(f"\n  over: {v['family']} {v['metric']} {show(v['measured'])} > {v['limit']}" for v in over))
    print(f"silently accepted critical defects: {len(silent)}" + "".join(f"\n  {c}" for c in silent))
    if args.json:
        measurement = {
            "schema": "sverity-accuracy/1", "tool": version,
            "provenance": provenance,
            "budgets_sha256": hashlib.sha256(budgets_path.read_bytes()).hexdigest() if budgets_path else None,
            "families": table, "classes": classes, "class_contracts": class_contracts(budgets), "budgets": verdicts, "silent_critical": silent, "cases": rows, "problems": problems,
        }
        if output:
            with output:
                output.write(json.dumps(measurement, indent=1, sort_keys=True) + "\n")
        else:
            args.json.write_text(json.dumps(measurement, indent=1, sort_keys=True) + "\n")
    for line in problems:
        print(f"problem: {line}", file=sys.stderr)
    if args.check and (silent or problems or over or unmeasured):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
