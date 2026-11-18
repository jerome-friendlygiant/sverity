#!/usr/bin/env python3
"""Reports how far each catalog rule is from `validated` (RFC section 8), from the repository alone.

The catalog says what is claimed; this checks what backs the claim: a spec block in the family doc and fixtures of
each required shape. Usage:

    conformance.py [ROOT]               print a table per family and a summary per deck
    conformance.py --check [ROOT]       fail when a rule breaks a requirement it has already reached
    conformance.py --rule CDC-003 [ROOT] print what one rule still lacks
    conformance.py --classes [ROOT]     print each lint workflow defect class, its rules and what they lack

--check enforces, for every implemented rule, a spec block and a must-not-fire assertion, and for implemented CDC and RDC
rules a mutation pair. For a validated rule it also requires every shape, a written severity and a recorded review.
"""
import json
import pathlib
import re
import sys

SHAPES = ("good", "bad", "boundary", "unknown", "mutation")


DECKS = ("core", "domain", "1.0")   # cumulative
CLASSES_DOC = "docs/rules/LINT_WORKFLOW.md"   # the lint workflow deck: rules with a defect class


def load_decks(root):
    """Deck name -> rule IDs. The catalog records the earliest deck of each rule."""
    catalog = json.loads((root / "docs/rules/catalog.json").read_text())["rules"]
    decks = {}
    for position, name in enumerate(DECKS):
        decks[name] = [rule["id"] for rule in catalog if rule.get("deck") in DECKS[: position + 1]]
    decks["lint"] = [rule["id"] for rule in catalog if rule.get("classes")]
    return decks


def defect_classes(root):
    """Class ID -> (defect, critical) from the workflow table."""
    found = {}
    for match in re.finditer(r"^\| `(LC\d\d)` \| (.*?) \| (yes|no) \|", (root / CLASSES_DOC).read_text(), re.M):
        found[match.group(1)] = (match.group(2), match.group(3) == "yes")
    return found


def spec_blocks(root):
    blocks = {}
    for family in ("LNT", "CDC", "RDC", "CST"):
        text = (root / f"docs/rules/{family}.md").read_text()
        for match in re.finditer(r"^### ([A-Z]+-\d+)\n(.*?)(?=^##|\Z)", text, re.S | re.M):
            blocks[match.group(1)] = match.group(2)
    return blocks


def info_by_policy(block):
    """True when the spec's Severity line makes INFO the rule's finding (LNT-074), so an INFO expectation is its bad case."""
    match = re.search(r"\*\*Severity:\*\* (\w+)", block)
    return bool(match) and match.group(1) == "INFO"


def fixture_shapes(root, rule, info_is_bad=False):
    """Which shapes the rule's own directory shows. Annotations decide, not only file names."""
    shapes = set()
    bad_severities = "ERROR|WARNING|INFO" if info_is_bad else "ERROR|WARNING"
    directory = root / "tests/fixtures" / rule
    if not directory.is_dir():
        return shapes
    for path in directory.iterdir():
        # The fixture runner writes and deletes __revert_* files beside each fixture while tests run in parallel.
        if path.suffix not in (".sv", ".v") or path.name.startswith("__revert_"):
            continue
        text = path.read_text(errors="replace")
        name = path.name
        # Scoped to WARNING is still must-not-fire where the safe design has a recognised INFO by spec.
        if re.search(rf"// expect-none: {rule}\b(?! (INFO|ERROR|FATAL))", text):
            shapes.add("good")
        if re.search(rf"// expect: {rule} ({bad_severities})", text) and not name.startswith(("unknown_", "boundary_")):
            shapes.add("bad")
        if name.startswith("boundary_"):
            shapes.add("boundary")
        if name.startswith(("unknown_", "hierarchy_unknown")) or "expect-coverage" in text:
            shapes.add("unknown")
        if "// revert:" in text:
            shapes.add("mutation")
    return shapes


def assess(root):
    catalog = json.loads((root / "docs/rules/catalog.json").read_text())["rules"]
    blocks = spec_blocks(root)
    rows = []
    for rule in catalog:
        rid = rule["id"]
        block = blocks.get(rid, "")
        shapes = fixture_shapes(root, rid, info_by_policy(block))
        rows.append({
            "id": rid,
            "status": rule["status"],
            "gap": rule.get("gap", ""),
            "review": rule.get("review"),
            "deck": rule.get("deck", ""),
            "deck_exclusion": rule.get("deck_exclusion", ""),
            "classes": rule.get("classes", []),
            "spec": bool(block),
            "severity": "**Severity:**" in block,
            "unknown_outcome": "**Unknown:**" in block,
            "shapes": shapes,
            "missing": [s for s in SHAPES if s not in shapes],
        })
    return rows


def problems(rows):
    """Requirements already reached that must not regress."""
    found = []
    for row in rows:
        rid, status = row["id"], row["status"]
        if row["deck"] not in DECKS and not row["deck_exclusion"]:
            found.append(f"{rid}: names neither a deck (core, domain, 1.0) nor a deck_exclusion reason")
        if status in ("implemented", "validated"):
            if not row["spec"]:
                found.append(f"{rid}: {status} but has no spec block in its family doc")
            if "good" not in row["shapes"]:
                found.append(f"{rid}: {status} but has no must-not-fire assertion (`// expect-none: {rid}`)")
            if rid[:3] in ("CDC", "RDC") and "mutation" not in row["shapes"]:
                found.append(f"{rid}: {status} but has no mutation pair (`// revert:`)")
        if status == "validated":
            if row["gap"]:
                found.append(f"{rid}: validated but still states a gap")
            if not row["severity"] or not row["unknown_outcome"]:
                found.append(f"{rid}: validated without a written severity and unknown outcome")
            if row["missing"]:
                found.append(f"{rid}: validated but lacks fixture shapes: {', '.join(row['missing'])}")
            review = row["review"] or {}
            if not all(review.get(key) for key in ("reviewer", "date", "report")):
                found.append(f"{rid}: validated without a recorded review (reviewer, date, report)")
    return found


OBLIGATION_SOURCES = "src/rules/lint"


def obligation_problems(root):
    """An obligation id a lint source emits is declared in the catalog, every declaration is emitted, and the
    rule's spec block names it. Without a declaration the finding is a gap, or gates when it names an unknown id."""
    catalog = json.loads((root / "docs/rules/catalog.json").read_text())["rules"]
    blocks = spec_blocks(root)
    declared = {}
    found = []
    for rule in catalog:
        for item in rule.get("obligations", []):
            declared.setdefault(item.get("id", ""), []).append(rule["id"])
            if not rule["id"].startswith("LNT-"):
                found.append(f"{rule['id']}: only lint (LNT) rules may declare obligations")
            if not item.get("id"):
                found.append(f"{rule['id']}: an obligation needs an id")
            elif f"`{item['id']}`" not in blocks.get(rule["id"], ""):
                found.append(f"{rule['id']}: the spec block does not name obligation `{item['id']}`")
    emitted = set()
    for path in sorted((root / OBLIGATION_SOURCES).glob("*.cpp")):
        emitted |= set(re.findall(r"\.obligation = [^;{}]*?\"([a-z][a-z0-9-]*)\"", path.read_text()))
    found += [f"obligation '{i}' is emitted by {OBLIGATION_SOURCES} but no catalog rule declares it" for i in sorted(emitted - set(declared))]
    found += [f"obligation '{i}' is declared by {', '.join(declared[i])} but no source emits it" for i in sorted(set(declared) - emitted)]
    return found


def class_problems(root, rows):
    """Every class of the workflow is covered by some rule, and every rule's class exists."""
    classes = defect_classes(root)
    found = [] if classes else [f"{CLASSES_DOC}: no defect classes found"]
    used = {k for row in rows for k in row["classes"]}
    found += [f"{k}: defect class with no rule" for k in sorted(set(classes) - used)]
    found += [f"{row['id']}: names unknown defect class {k}" for row in rows for k in row["classes"] if k not in classes]
    return found


def class_table(root, rows):
    for k, (defect, critical) in defect_classes(root).items():
        mine = [row for row in rows if k in row["classes"]]
        ready = sum(1 for row in mine if row["status"] == "validated")
        print(f"{k} {'critical' if critical else 'hygiene '} {len(mine):2} rules, {ready} validated  {defect}")
        for row in mine:
            print(f"      {row['id']} {row['status']:12} {'gap ' if row['gap'] else ''}missing: {','.join(row['missing']) or 'none'}")


def summary(root, rows):
    by_id = {row["id"]: row for row in rows}
    print(f"{'rule':9} {'status':12} spec sev  unk  fixtures missing")
    for row in rows:
        yes = lambda value: "yes " if value else "-   "
        print(f"{row['id']:9} {row['status']:12} {yes(row['spec'])} {yes(row['severity'])} {yes(row['unknown_outcome'])} "
              f"{','.join(row['missing']) or 'none'}")
    print()
    for name, deck in load_decks(root).items():
        rules = [by_id[rule] for rule in deck]
        ready = sum(1 for row in rules if row["status"] == "validated")
        shaped = sum(1 for row in rules if not row["missing"] and row["severity"] and row["unknown_outcome"])
        print(f"deck {name}: {len(rules)} rules, {ready} validated, {shaped} with every fixture shape and a written severity")


def main(argv):
    args = [arg for arg in argv if not arg.startswith("--")]
    if "--rule" in argv:
        wanted = argv[argv.index("--rule") + 1]
        args.remove(wanted)
    root = pathlib.Path(args[0] if args else pathlib.Path(__file__).resolve().parents[2])
    rows = assess(root)
    if "--rule" in argv:
        row = next((r for r in rows if r["id"] == wanted), None)
        if not row:
            print(f"unknown rule {wanted}", file=sys.stderr)
            return 1
        print(json.dumps({**row, "shapes": sorted(row["shapes"])}, indent=2))
        return 0
    if "--check" in argv:
        found = problems(rows) + class_problems(root, rows) + obligation_problems(root)
        for line in found:
            print(line, file=sys.stderr)
        print(f"conformance: {len(rows)} rules, {len(found)} problem(s)")
        return 1 if found else 0
    if "--classes" in argv:
        class_table(root, rows)
        return 0
    summary(root, rows)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
