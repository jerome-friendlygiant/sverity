#!/usr/bin/env python3
"""Migrates a Verilator lint policy (.vlt) into SVerity waivers with the same scope (docs/rules/LINT_WORKFLOW.md).

    migrate_verilator.py CONFIG.vlt [CONFIG.vlt ...] --root DIR --waivers OUT.json --report OUT.md [--owner NAME]
                         [--config OUT.json]

Reads `lint_off` / `lint_on` directives: `-rule R` (or legacy `-msg R`), `-file "glob"`, `-match "pattern"`,
`-lines N` or `-lines N-M`. Other directives are not lint policy and are only counted. A Verilator code maps to SVerity
rules through RULES_FOR_CODE, each mapping justified against its rule's trigger in docs/rules/LNT.md.

A directive is migrated only when its whole scope survives:
- `-file` (other than "*") becomes the waiver's `file`, a glob relative to --root (SVerity globs `*` across `/`).
- `-match` must name exactly one quoted identifier, e.g. `Signal is not used: 'clk_i'`. It becomes
  `hierarchy: "*.clk_i"`, not `signal`: SVerity's `signal` matches only findings that carry `signals`, and slang's
  lint findings (LNT-011 for an unused variable, say) carry only their occurrence path, which `hierarchy` matches.
  Any other text in the pattern must be one of the code's own message stems (MESSAGE_STEMS); anything else would
  be dropped and widen the waiver, so the directive is not migrated.
- `-lines N` gives `line`; `-lines N-M` gives one waiver per line when M-N < 50.
A directive with no file, hierarchy or signal narrowing is a global policy: listed, never written as a waiver. A policy
with an exact SVerity option (POLICY_OPTIONS) is written to --config as that option; one that is narrower than the
Verilator code, never wider, counts as exact (the rest of the code's scope stays reported).
`lint_on` cancels a `lint_off` with the same rule and scope; any other `lint_on` of the same rule makes the earlier
`lint_off` uncertain, so it is not migrated.

Output is deterministic. Exit 0 unless an input cannot be read (exit 2).
"""
import argparse
import json
import os
import re
import sys
from collections import Counter

# Global `lint_off -rule CODE` -> SVerity rule option. PINCONNECTEMPTY covers empty connections of every direction;
# LNT-010.empty_output makes only empty outputs advisory, so an empty input still warns (narrower, never wider).
POLICY_OPTIONS = {
    "PINCONNECTEMPTY": ("LNT-010.empty_output", "info"),
}

# Verilator code -> SVerity rules. Each entry names the LNT.md clause that justifies it; LINT_WORKFLOW.md gives the class.
RULES_FOR_CODE = {
    # LNT-004: slang width-trunc/width-expand. LNT-035, -036, -056, -073: LC02 width loss at a shift, a product or
    # carry, an unsized concatenation operand, or a narrower literal (the brief's LC02 set).
    "WIDTH": ("LNT-004", "LNT-035", "LNT-036", "LNT-056", "LNT-073"),
    "WIDTHTRUNC": ("LNT-004", "LNT-035", "LNT-036", "LNT-056", "LNT-073"),
    "WIDTHEXPAND": ("LNT-004", "LNT-035", "LNT-036", "LNT-056", "LNT-073"),
    "WIDTHCONCAT": ("LNT-004", "LNT-035", "LNT-036", "LNT-056", "LNT-073"),
    # LNT-013 (a): slang inferred-latch. NOLATCH sits in LC11 with LATCH in LINT_WORKFLOW.md.
    "LATCH": ("LNT-013",),
    "NOLATCH": ("LNT-013",),
    # LNT-062: a nonblocking assignment to a module variable inside a combinational procedure.
    "COMBDLY": ("LNT-062",),
    # LNT-015: a blocking assignment to a module variable inside an edge-triggered procedure.
    "BLKSEQ": ("LNT-015",),
    # LNT-063: a module variable with both blocking and nonblocking assignments.
    "BLKANDNBLK": ("LNT-063",),
    # LNT-044: a variable read before a later write in a combinational procedure.
    "ALWCOMBORDER": ("LNT-044",),
    # LNT-065: slang case-incomplete.
    "CASEINCOMPLETE": ("LNT-065",),
    # LNT-032: slang case-overlap and case-dup (LNT-032 trigger and Exempt list; slang_rule_map.cpp).
    "CASEOVERLAP": ("LNT-032",),
    # LNT-016: a casex statement.
    "CASEX": ("LNT-016",),
    # LNT-040 (b)-(d): an X or Z literal as a case item.
    "CASEWITHX": ("LNT-040",),
    # LNT-038 (a): a comparison that is constant for every value of the operand's domain.
    "CMPCONST": ("LNT-038",),
    "UNSIGNED": ("LNT-038",),
    # LNT-009 (a): slang multiple-drivers options and MultipleUWireDrivers/MultipleUDNTDrivers.
    "MULTIDRIVEN": ("LNT-009",),
    # LNT-008: undriven bits of a net or variable; LNT-024: an output port of a user module with no driver.
    "UNDRIVEN": ("LNT-008", "LNT-024"),
    # LNT-011 (a): slang unused-* options.
    "UNUSED": ("LNT-011",),
    "UNUSEDSIGNAL": ("LNT-011",),
    "UNUSEDPARAM": ("LNT-011",),
    "UNUSEDGENVAR": ("LNT-011",),
    # LNT-012: a combinational loop (its trigger, strongly connected components). Verilator's UNOPTFLAT is mostly
    # scheduling noise that split_var removes; the migrated waiver keeps the reference team's scope on this rule.
    "UNOPTFLAT": ("LNT-012",),
    # LNT-010 (a): unconnected-input/output/inout-port and empty-connection options.
    "PINMISSING": ("LNT-010",),
    "PINCONNECTEMPTY": ("LNT-010",),
    "PINNOCONNECT": ("LNT-010",),
    # LNT-006 (a): constant select out of range.
    "SELRANGE": ("LNT-006",),
    # LNT-050: InputPortAssign (an assignment to an input port).
    "ASSIGNIN": ("LNT-050",),
    # LNT-007 (implicit-net, implicit-net-port).
    "IMPLICIT": ("LNT-007",),
    # LNT-019: `#` delays in user code (statement, intra-assignment and initial delays).
    "STMTDLY": ("LNT-019",),
    "ASSIGNDLY": ("LNT-019",),
    "INITIALDLY": ("LNT-019",),
    # LNT-078: shadow-value (a name that hides an outer one).
    "VARHIDDEN": ("LNT-078",),
}

# Codes with no SVerity counterpart. Style and naming are outside 1.0 scope (LNT.md crosswalk); simulator scheduling
# is not a defect. PROCASSINIT is left unmapped: no LNT.md trigger was verified to describe it.
NO_COUNTERPART = {
    "DECLFILENAME": "style and file naming (outside 1.0 scope)",
    "EOFNEWLINE": "style and formatting (outside 1.0 scope)",
    "SYMRSVDWORD": "C++ naming of a symbol (outside 1.0 scope)",
    "IMPERFECTSCH": "simulator scheduling only",
    "UNOPTTHREADS": "simulator scheduling only",
    "PROCASSINIT": "no LNT.md trigger verified for this message",
}

# Message text Verilator puts around the quoted signal. Text of a -match that is not one of these (or glob) would be
# dropped by the migration, which widens the waiver, so the directive is not migrated.
MESSAGE_STEMS = {
    "UNUSED": ("Signal is not used", "Bits of signal are not used", "Parameter is not used"),
    "UNUSEDSIGNAL": ("Signal is not used", "Bits of signal are not used"),
    "UNUSEDPARAM": ("Parameter is not used",),
    "UNDRIVEN": ("Signal is not driven",),
    "MULTIDRIVEN": ("Signal has multiple driving blocks with different clocking",),
    "UNOPTFLAT": ("Signal unoptimizable: Feedback to clock or circular logic",
                  "Signal unoptimizable: Circular combinational logic"),
    "ALWCOMBORDER": ("Always_comb variable driven after use",),
}

OPTIONS_WITH_VALUE = {"-rule": "rule", "-msg": "rule", "-file": "file", "-match": "match", "-lines": "lines"}
QUOTED = re.compile(r"(?<![A-Za-z0-9_])'([^'\s]*)'(?![A-Za-z0-9_])")
LINES = re.compile(r"^(\d+)(?:-(\d+))?$")
MAX_LINE_RANGE = 50       # M-N < 50 gives one waiver per line; a wider range is not migrated
REQUIRED = "<REQUIRED>"


def alnum(text):
    return re.sub(r"[^a-z0-9]", "", text.lower())


def only_wildcards(text):
    return bool(text) and set(text) <= set("*?")


def glob_regex(glob):
    """SVerity's glob: `*` and `?` only, `*` also crosses `/`. fnmatch would also read `[`, so it is not used."""
    return re.compile("".join(".*" if c == "*" else "." if c == "?" else re.escape(c) for c in glob) + r"\Z")


def scan(text):
    """Returns ([(line, statement, comment_directly_above)], [problems]). Comments and `;` / newlines split statements.

    Quoted strings are kept whole, so a `//` or `;` inside them is not a comment or a separator. A comment block
    counts as directly above a statement only when it ends on the line before the statement starts."""
    out, problems = [], []
    cur, cur_line = [], None
    comment, comment_end = [], None
    line, i, n = 1, 0, len(text)

    def add_comment(body, first, last):
        nonlocal comment, comment_end
        if comment_end is not None and first > comment_end + 1:
            comment = []
        comment.append(" ".join(body.split()))
        comment_end = last

    def end_statement():
        nonlocal cur, cur_line, comment, comment_end
        stmt = "".join(cur).strip()
        if stmt:
            above = " ".join(part for part in comment if part) if comment and comment_end == cur_line - 1 else ""
            out.append((cur_line, stmt, above))
            comment, comment_end = [], None
        cur, cur_line = [], None

    while i < n:
        c = text[i]
        if c == "\n":
            end_statement()
            line += 1
            i += 1
        elif c == ";":
            end_statement()
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            if not cur:
                add_comment(text[i + 2:j], line, line)
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            end = n if j < 0 else j
            if j < 0:
                problems.append((line, "unterminated /* comment"))
            last = line + text.count("\n", i, end)
            if not cur:
                add_comment(text[i + 2:end], line, last)
            line = last
            i = n if j < 0 else j + 2
        elif c == '"':
            j = i + 1
            while j < n and text[j] not in '"\n':
                j += 2 if text[j] == "\\" and j + 1 < n and text[j + 1] != "\n" else 1
            stop = j + 1 if j < n and text[j] == '"' else j
            if cur_line is None:
                cur_line = line
            cur.append(text[i:stop])
            i = stop
        elif c in " \t\r\f\v":
            if cur:
                cur.append(c)
            i += 1
        else:
            if cur_line is None:
                cur_line = line
            cur.append(c)
            i += 1
    end_statement()
    return out, problems


def tokenize(stmt):
    """[('word'|'str', text)] or None with an error message."""
    toks, i, n = [], 0, len(stmt)
    while i < n:
        c = stmt[i]
        if c.isspace():
            i += 1
        elif c == '"':
            j = i + 1
            while j < n and stmt[j] != '"':
                j += 2 if stmt[j] == "\\" and j + 1 < n else 1
            if j >= n:
                return None, "unterminated string"
            toks.append(("str", stmt[i + 1:j]))
            i = j + 1
        else:
            j = i
            while j < n and not stmt[j].isspace() and stmt[j] != '"':
                j += 1
            toks.append(("word", stmt[i:j]))
            i = j
    return toks, None


class Directive:
    def __init__(self, vlt, line, index, text, above, kind):
        self.vlt, self.line, self.index, self.text, self.above, self.kind = vlt, line, index, text, above, kind
        self.keyword = None
        self.rule = self.file = self.match = self.lines = None
        self.error = None
        self.cancelled_by = None
        self.uncertain = None

    def scope(self):
        return (self.rule, self.file, self.match, self.lines)


def parse_statement(vlt, line, index, stmt, above):
    toks, err = tokenize(stmt)
    if err:
        d = Directive(vlt, line, index, stmt, above, "malformed")
        d.error = err
        return d
    if not toks:
        return None
    if toks[0][0] != "word":
        d = Directive(vlt, line, index, stmt, above, "malformed")
        d.error = "statement does not start with a keyword"
        return d
    keyword = toks[0][1].lstrip("`")
    if keyword == "verilator_config":
        return None
    if keyword not in ("lint_off", "lint_on"):
        d = Directive(vlt, line, index, stmt, above, "other")
        d.keyword = keyword
        return d
    d = Directive(vlt, line, index, stmt, above, keyword)
    d.keyword = keyword
    seen = set()
    i = 1
    while i < len(toks):
        kind, value = toks[i]
        if kind != "word" or value not in OPTIONS_WITH_VALUE:
            d.error = f"unsupported or unexpected argument {value!r}"
            return d
        if i + 1 >= len(toks):
            d.error = f"{value} has no value"
            return d
        field = OPTIONS_WITH_VALUE[value]
        if field in seen:
            d.error = f"duplicate {field} option"
            return d
        seen.add(field)
        raw = toks[i + 1][1].strip()
        if field == "rule":
            d.rule = raw
        elif field == "file":
            d.file = None if (not raw or only_wildcards(raw)) else raw
        elif field == "match":
            d.match = None if raw in ("", "*") else raw
        else:
            m = LINES.match(raw)
            if not m or int(m.group(1)) < 1 or (m.group(2) and int(m.group(2)) < int(m.group(1))):
                d.error = f"bad -lines value {raw!r}"
                return d
            lo = int(m.group(1))
            d.lines = (lo, int(m.group(2) or m.group(1)))
        i += 2
    return d


def translate_match(code, pattern):
    """(hierarchy glob or None, reason or None). None, None means the pattern restricts nothing."""
    text = pattern.strip()
    if text in ("", "*"):
        return None, None
    found = list(QUOTED.finditer(text))
    if not found:
        return None, "message pattern names no quoted signal (Verilator quotes signal names)"
    if len(found) > 1:
        return None, f"message pattern names {len(found)} quoted identifiers"
    m = found[0]
    name = m.group(1)
    before, after = text[:m.start()], text[m.end():]
    idx = re.match(r"\[[^\]\s]*\]", after)
    if idx:
        name += idx.group(0)
        after = after[idx.end():]
    residue = before + after
    if alnum(residue) and alnum(residue) not in {alnum(stem) for stem in MESSAGE_STEMS.get(code, ())}:
        return None, "message text beyond the signal name would be dropped (not a message form of this code)"
    if name.endswith("[*]"):
        name = name[:-3]          # every index: the whole signal, the same scope
    if not re.search(r"[A-Za-z0-9_]", name):
        return None, "signal pattern has no name (glob only)"
    if re.search(r"[\[\]|,]", name):
        return None, "bit or part select, or several names, in the signal pattern"
    if "." in name:
        if name.startswith("*."):
            return name, None
        return None, "hierarchical name relative to the Verilator top; SVerity's hierarchy root differs"
    return "*." + name, None


def display(path, root):
    absolute = os.path.abspath(path)
    if absolute == root or absolute.startswith(root + os.sep):
        return os.path.relpath(absolute, root).replace(os.sep, "/")
    return os.path.basename(path)


def resolve_file(glob, root):
    g = glob.strip()
    if g.startswith("./"):
        g = g[2:]
    if g.startswith("/"):
        prefix = root.rstrip("/") + "/"
        if not g.startswith(prefix):
            return None, "absolute -file path outside --root"
        g = g[len(prefix):]
    return g, None


def list_files(root):
    found = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        for name in filenames:
            found.append(os.path.relpath(os.path.join(dirpath, name), root).replace(os.sep, "/"))
    return sorted(found)


def migrate(texts, root, owner):
    seq, nonlint = [], Counter()
    for vlt, text in texts:
        statements, problems = scan(text)
        for line, message in problems:
            d = Directive(vlt, line, len(seq), message, "", "malformed")
            d.error = message
            seq.append(d)
        for line, stmt, above in statements:
            d = parse_statement(vlt, line, len(seq), stmt, above)
            if d is None:
                continue
            if d.kind == "other":
                nonlint[d.keyword] += 1
                continue
            seq.append(d)

    # lint_on cancels an exact lint_off; another lint_on of the same rule leaves that lint_off uncertain.
    active, lint_on_no_effect = [], 0
    for d in seq:
        if d.kind == "lint_off" and d.error is None:
            active.append(d)
        elif d.kind == "lint_on" and d.error is None:
            if d.rule is None:
                for o in active:
                    o.uncertain = d
                continue
            same = [o for o in active if o.rule == d.rule]
            if not same:
                lint_on_no_effect += 1
            exact = [o for o in same if o.scope() == d.scope()]
            for o in same:
                if any(o is e for e in exact):
                    o.cancelled_by = d
                else:
                    o.uncertain = d
            active = [o for o in active if not any(o is e for e in exact)]

    file_list = None
    rows, waivers = [], []
    for d in seq:
        if d.kind == "malformed":
            rows.append(dict(vlt=d.vlt, line=d.line, text=d.text, code="", status="not migrated",
                             reason=f"malformed: {d.error}", rules=(), hits=None))
            continue
        if d.kind != "lint_off":
            continue
        row = dict(vlt=d.vlt, line=d.line, text=d.text, code=d.rule or "", status="", reason="", rules=(), hits=None)
        rows.append(row)
        if d.error:
            row.update(status="not migrated", reason=f"malformed: {d.error}")
            continue
        if d.uncertain is not None:
            row.update(status="not migrated", reason=f"a lint_on of the same rule at line {d.uncertain.line} has another "
                                                     "scope; not modelled")
            continue
        if d.cancelled_by is not None:
            row.update(status="cancelled", reason=f"cancelled by lint_on at line {d.cancelled_by.line}")
            continue
        if d.rule is None:
            row.update(status="not migrated", reason="lint_off without -rule applies to every code")
            continue
        code = d.rule
        if code in NO_COUNTERPART:
            row.update(status="not migrated", reason=f"no SVerity counterpart: {NO_COUNTERPART[code]}")
            continue
        if code not in RULES_FOR_CODE:
            row.update(status="not migrated", reason="code not in the migration table")
            continue
        rules = RULES_FOR_CODE[code]
        row["rules"] = rules
        file_glob = None
        if d.file is not None:
            file_glob, err = resolve_file(d.file, root)
            if err:
                row.update(status="not migrated", reason=err)
                continue
            if only_wildcards(file_glob):
                file_glob = None
        hierarchy, err = translate_match(code, d.match) if d.match is not None else (None, None)
        if err:
            row.update(status="not migrated", reason=err)
            continue
        lines = [None]
        if d.lines is not None:
            lo, hi = d.lines
            if hi - lo >= MAX_LINE_RANGE:
                row.update(status="not migrated", reason=f"line range of {hi - lo + 1} lines (50 or more)")
                continue
            lines = list(range(lo, hi + 1))
        if file_glob is None and hierarchy is None:
            option = POLICY_OPTIONS.get(code) if d.match in (None, "*") and d.lines is None else None
            row.update(status="global policy", reason="no file, hierarchy or signal narrowing",
                       option=f"{option[0]}={option[1]}" if option else None)
            continue
        if file_glob is not None:
            if file_list is None:
                file_list = list_files(root)
            rx = glob_regex(file_glob)
            row["hits"] = sum(1 for f in file_list if rx.match(f))
        reason = f"migrated from {d.vlt}:{d.line}: {d.above or 'Verilator lint_off ' + code}"
        for rule in rules:
            for ln in lines:
                w = {"id": f"vlt:{d.vlt}:{d.line}:{d.index}:{rule}" + (f":{ln}" if ln else ""),
                     "rule": rule, "owner": owner, "reason": reason}
                if file_glob is not None:
                    w["file"] = file_glob
                if hierarchy is not None:
                    w["hierarchy"] = hierarchy
                if ln is not None:
                    w["line"] = ln
                waivers.append(((d.vlt, d.line, d.index, rule, ln or 0), w, row))
        row.update(status="migrated", rules=rules)

    # One waiver per rule and scope: SVerity rejects duplicate scopes (CST-006), so later duplicates are merged.
    waivers.sort(key=lambda item: item[0])
    kept, merged, seen = [], [], {}
    for key, w, row in waivers:
        scope = (w["rule"], w.get("file"), w.get("hierarchy"), w.get("line"))
        if scope in seen:
            merged.append((w["rule"], scope, seen[scope]["id"], w["id"]))
            continue
        seen[scope] = w
        kept.append((w, row))
    return dict(rows=rows, kept=kept, merged=merged, nonlint=nonlint, lint_on_no_effect=lint_on_no_effect)


def write_waivers(path, kept):
    entries = []
    for w, _ in kept:
        entries.append({k: w[k] for k in sorted(w)})
    with open(path, "w", encoding="utf-8") as out:
        json.dump({"waivers": entries}, out, indent=2, sort_keys=True)
        out.write("\n")


def cell(text):
    return str(text).replace("|", "\\|").replace("\n", " ")


def write_report(path, result, configs, root, owner):
    rows, kept = result["rows"], result["kept"]
    status = Counter(r["status"] for r in rows)
    lines = ["# Verilator policy migration", "",
             f"Configurations: {len(configs)} ({', '.join(configs)})", "",
             f"Owner written to every waiver: `{owner}`. Waivers that still carry `{REQUIRED}` are not applied by SVerity.",
             "", "## Counts", "", "| Outcome | Count |", "|---|---:|",
             f"| directives migrated | {status['migrated']} |",
             f"| waivers written (after merging duplicates) | {len(kept)} |",
             f"| not migrated | {status['not migrated']} |",
             f"| global policy (listed below, not a waiver) | {status['global policy']} |",
             f"| cancelled by lint_on | {status['cancelled']} |",
             f"| lint_on directives with no lint_off of the rule | {result['lint_on_no_effect']} |",
             f"| not lint policy (counted by keyword below) | {sum(result['nonlint'].values())} |",
             f"| merged duplicate waivers | {len(result['merged'])} |", "",
             "## Not migrated", "", "| Source | Directive | Reason |", "|---|---|---|"]
    for r in sorted((r for r in rows if r["status"] == "not migrated"), key=lambda r: (r["vlt"], r["line"])):
        lines.append(f"| {cell(r['vlt'])}:{r['line']} | `{cell(r['text'][:160])}` | {cell(r['reason'])} |")
    lines += ["", "## Cancelled by lint_on", "", "| Source | Directive | Reason |", "|---|---|---|"]
    for r in sorted((r for r in rows if r["status"] == "cancelled"), key=lambda r: (r["vlt"], r["line"])):
        lines.append(f"| {cell(r['vlt'])}:{r['line']} | `{cell(r['text'][:160])}` | {cell(r['reason'])} |")
    lines += ["", "## Global policies", "",
              "No waiver is written for these: each has no file, hierarchy or signal narrowing. A policy with an exact "
              "SVerity option is written to --config as that option; the others are not expressed, because the rules "
              "they map to cover more than the Verilator code.", "",
              "| Source | Directive | SVerity rules | Option written |", "|---|---|---|---|"]
    for r in sorted((r for r in rows if r["status"] == "global policy"), key=lambda r: (r["vlt"], r["line"])):
        rules = RULES_FOR_CODE.get(r["code"], ())
        lines.append(f"| {cell(r['vlt'])}:{r['line']} | `{cell(r['text'][:160])}` | {', '.join(rules) or '-'} | "
                     f"{('`' + r['option'] + '`') if r.get('option') else 'none'} |")
    lines += ["", "## Per Verilator code", "", "| Code | SVerity rules | Migrated | Not migrated | Global policy |",
              "|---|---|---:|---:|---:|"]
    codes = sorted({r["code"] for r in rows if r["code"]})
    for code in codes:
        rs = [r for r in rows if r["code"] == code]
        rules = ", ".join(RULES_FOR_CODE.get(code, ())) or ("none: " + NO_COUNTERPART[code] if code in NO_COUNTERPART
                                                              else "not in table")
        lines.append(f"| {code} | {rules} | {sum(r['status'] == 'migrated' for r in rs)} | "
                     f"{sum(r['status'] == 'not migrated' for r in rs)} | {sum(r['status'] == 'global policy' for r in rs)} |")
    lines += ["", "## Not lint policy", "", "| Keyword | Directives |", "|---|---:|"]
    for keyword, count in sorted(result["nonlint"].items()):
        lines.append(f"| {cell(keyword)} | {count} |")
    lines += ["", "## Waivers whose file glob matches no file under --root", ""]
    empty = sorted({w["file"] for w, row in kept if "file" in w and row.get("hits") == 0})
    lines += [f"- `{g}`" for g in empty] or ["None."]
    lines += ["", "## Merged duplicates", ""]
    lines += [f"- {rule} {scope}: `{dropped_id}` merged into `{kept_id}`"
              for rule, scope, kept_id, dropped_id in result["merged"]] or ["None."]
    lines += ["", "## Notes", "",
              "- `-match` signals become `hierarchy: \"*.NAME\"`: SVerity's `signal` field does not match slang lint findings.",
              "- A `hierarchy` waiver does not apply to a coalesced slang finding (SVerity reports it as stale).",
              "- Configurations are read in the order given; a `lint_on` cancels only in that order, within the same rule.",
              ""]
    with open(path, "w", encoding="utf-8") as out:
        out.write("\n".join(lines))


def main(argv=None):
    ap = argparse.ArgumentParser(description="Migrate a Verilator lint policy into SVerity waivers.")
    ap.add_argument("configs", nargs="+", metavar="CONFIG.vlt")
    ap.add_argument("--root", required=True, help="checkout the -file globs are relative to")
    ap.add_argument("--waivers", required=True, help="output: SVerity waiver file (JSON)")
    ap.add_argument("--report", required=True, help="output: Markdown report")
    ap.add_argument("--owner", default=REQUIRED, help="owner written to every waiver (default: %(default)s)")
    ap.add_argument("--config", help="output: SVerity configuration holding the options of global policies")
    args = ap.parse_args(argv)
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"migrate_verilator: --root is not a directory", file=sys.stderr)
        return 2
    texts = []
    for path in args.configs:
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                texts.append((display(path, root), f.read()))
        except OSError as e:
            print(f"migrate_verilator: cannot read {display(path, root)}: {e.strerror}", file=sys.stderr)
            return 2
    result = migrate(texts, root, args.owner)
    try:
        write_waivers(args.waivers, result["kept"])
        write_report(args.report, result, [t[0] for t in texts], root, args.owner)
        if args.config:
            options = dict(sorted(r["option"].split("=", 1) for r in result["rows"] if r.get("option")))
            with open(args.config, "w", encoding="utf-8") as f:
                f.write(json.dumps({"options": options}, indent=2, sort_keys=True) + "\n")
    except OSError as e:
        print(f"migrate_verilator: cannot write output: {e.strerror}", file=sys.stderr)
        return 2
    status = Counter(r["status"] for r in result["rows"])
    print(f"migrated {status['migrated']}, not migrated {status['not migrated']}, "
          f"global policies {status['global policy']}, cancelled {status['cancelled']}; "
          f"{len(result['kept'])} waivers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
