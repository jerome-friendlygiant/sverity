#!/usr/bin/env python3
"""Seeded-mutation recall of the lint goal on the pinned reference IPs (docs/rules/LINT_WORKFLOW.md, defect classes).

    mutation_recall.py SVERITY SCRATCH [--replay DIR] [--ip uart --ip aes] [--per-op 3] [--seed 1] [--json OUT]
                       [--triage TRIAGE.json --reuse OUT.json]

DIR is the tree tools/dev/reference_lint.py wrote (DIR/lint/<ip>/{design.f,config.json,waivers.json,report.json}, the
OpenTitan checkout beside it); it is only read. For each IP the tool picks sites in the IP's own RTL (not prim_*, tlul_*,
packages, generated *_reg_top), applies ONE mutation per site to a copy under SCRATCH/mut (the mutation never adds or
removes a line), runs SVerity with the reference command line and the IP's own configuration and waivers, and scores each
site against an unmutated baseline run of the same binary. Each operator builds a defect of one critical class by
construction; a site whose precondition cannot be proved from the text is not offered. Several mutants share a run (at
most one per file, so one per module); the runs of one IP go one at a time under a memory cap.

Outcome per mutant: CAUGHT (a finding that is new against the baseline and gates: an unwaived ERROR/FATAL, or an
unwaived WARNING the analysis established or judged heuristically, at the site +-2 lines or naming the mutated signal in
the mutated file), CAUGHT_NONGATING (only a new INFO, obligation or not-analysed finding there: not caught for a critical
class), CAUGHT_WAIVED (only a finding a migrated waiver hides: not caught), MISSED, INVALID (the mutant does not
elaborate, so it is not a defect of its class and is not scored). Results hold counts and site keys (path in the
OpenTitan repository, line, operator, class), never RTL text. A MISSED mutant is read in context by a person and
classified (real miss, equivalent mutant, operator error); `--triage` merges that file into a stored result.
"""
import argparse
import bisect
import collections
import dataclasses
import hashlib
import json
import math
import pathlib
import random
import re
import shutil
import subprocess
import sys
import time
import types

SCHEMA = "sverity-mutation-recall/1"
HERE = pathlib.Path(__file__).resolve().parent
IPS = ["uart", "csrng", "rv_timer", "spi_host", "i2c", "aes", "hmac", "kmac", "otbn", "entropy_src"]
SOLO = {"LC17"}          # classes whose mutants each get a run of their own
CRITICAL = {f"LC{n:02d}" for n in (1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 24, 25)}


# ----------------------------------------------------------------------------------------------------------------------
# Masking and tokens
# ----------------------------------------------------------------------------------------------------------------------

_COMMENT = re.compile(r'"(?:\\.|[^"\\\n])*"|//[^\n]*|/\*.*?\*/', re.S)
_ATTRIBUTE = re.compile(r"\(\*\s[^*]*?\*\)")
_DIRECTIVE = re.compile(r"`(ifdef|ifndef|elsif|else|endif|define|include|timescale|undef|default_nettype|resetall|[A-Za-z_]\w*)")
_COND = re.compile(r"`(ifdef|ifndef|endif)\b")
_LINE_DIRECTIVES = ("include", "timescale", "undef", "default_nettype", "resetall", "elsif", "else", "endif")


def _blank(text):
    return re.sub(r"[^\n]", " ", text)


def mask_comments(text):
    """The text with comments blanked and string contents blanked; same length, same newlines."""
    def sub(match):
        s = match.group(0)
        return '"' + " " * (len(s) - 2) + '"' if s[0] == '"' else _blank(s)
    return _ATTRIBUTE.sub(lambda m: _blank(m.group(0)), _COMMENT.sub(sub, text))


def mask_directives(text):
    """Blanks conditional-compilation regions (both branches), macro definitions, includes and macro invocations: code
    the tool cannot tell active from inactive is never mutated or parsed."""
    out = list(text)
    n = len(text)

    def blank(a, b):
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    pos = 0
    while True:
        m = _DIRECTIVE.search(text, pos)
        if not m:
            break
        word, start = m.group(1), m.start()
        if word in ("ifdef", "ifndef"):
            level, j = 1, m.end()
            while level:
                m2 = _COND.search(text, j)
                if not m2:
                    j = n
                    break
                level += -1 if m2.group(1) == "endif" else 1
                j = m2.end()
            blank(start, j)
            pos = j
        elif word == "define" or word in _LINE_DIRECTIVES:
            j = m.end()
            while True:
                nl = text.find("\n", j)
                if nl == -1:
                    j = n
                    break
                if word == "define" and text[:nl].rstrip(" \t").endswith("\\"):
                    j = nl + 1
                    continue
                j = nl
                break
            blank(start, j)
            pos = j
        else:
            j = m.end()
            k = j
            while k < n and text[k] in " \t":
                k += 1
            if k < n and text[k] == "(":
                depth = 0
                while k < n:
                    depth += (text[k] == "(") - (text[k] == ")")
                    k += 1
                    if depth == 0:
                        break
                j = k
            blank(start, j)
            pos = j
    return "".join(out)


Tok = collections.namedtuple("Tok", "kind text start end")
_TOKEN = re.compile(r"""
  (?P<ws>\s+)
 |(?P<num>(?:\d[\d_]*)?'[sS]?[bBoOdDhH]\s*[0-9a-fA-FxXzZ?_]+|'[01xXzZ](?![\w])|\d[\d_]*(?:\.\d+)?(?:[eE][+-]?\d+)?)
 |(?P<id>[A-Za-z_$][\w$]*)
 |(?P<sym>\+:|-:|::|<<<=|>>>=|<<=|>>=|<<<|>>>|===|!==|==\?|!=\?|<=|>=|==|!=|&&|\|\||<<|>>|\+\+|--|\+=|-=|\*=|/=|%=|&=|\|=|\^=|\*\*|->|'\{|.)
""", re.X | re.S)


def tokenize(masked):
    return [Tok("num" if m.lastgroup == "num" else "id" if m.lastgroup == "id" else "sym", m.group(), m.start(), m.end())
            for m in _TOKEN.finditer(masked) if m.lastgroup != "ws"]


# ----------------------------------------------------------------------------------------------------------------------
# Source model: modules, declarations, procedural blocks, continuous assignments, instances
# ----------------------------------------------------------------------------------------------------------------------

class ParseError(Exception):
    pass


TYPE_KW = {"logic", "wire", "reg", "bit", "tri", "wand", "wor", "byte", "int", "integer", "shortint", "longint"}
NOT_A_TYPE = {"assign", "always", "always_comb", "always_ff", "always_latch", "initial", "final", "if", "else", "case",
              "casez", "casex", "begin", "end", "for", "foreach", "while", "repeat", "forever", "generate", "endgenerate",
              "genvar", "localparam", "parameter", "typedef", "function", "endfunction", "task", "endtask", "import",
              "export", "return", "break", "continue", "module", "endmodule", "input", "output", "inout", "assert",
              "assume", "cover", "property", "sequence", "default", "unique", "priority", "unique0", "disable", "wait",
              "force", "release", "deassign", "enum", "struct", "union", "package", "endpackage", "interface",
              "endinterface", "modport", "defparam", "specify", "primitive", "automatic", "static", "const", "var",
              "inside", "new", "this", "super", "null", "void", "string", "event", "real", "time", "signed", "unsigned"}
SEPARATORS = {";", "begin", "end", "generate", "endgenerate", "endfunction", "endtask", "endcase"}
ASSIGN_OPS = {"=", "<=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "<<<=", ">>>="}


@dataclasses.dataclass
class Decl:
    name: str
    kind: str                # "port" or "local"
    direction: str           # "input", "output", "inout" or ""
    typekw: str              # a built-in type keyword, or ""
    typename: str            # a user type name (enum, struct, package type), or ""
    packed: tuple            # normalised text of each packed dimension
    signed: bool
    unpacked: bool
    tok: int
    kw: int = -1             # token of the type keyword in a local declaration statement
    names: int = 1           # how many names that statement declares


class Module:
    def __init__(self, name, start, end, body):
        self.name, self.start, self.end, self.body = name, start, end, body   # tokens: 'module', 'endmodule', first body
        self.decls = collections.defaultdict(list)
        self.ports = []                                                       # (name, direction) in order
        self.always = []
        self.cassigns = []
        self.instances = []

    def decl(self, name):
        found = self.decls.get(name, [])
        return found[0] if len(found) == 1 else None


class Source:
    def __init__(self, text):
        self.text = text
        comments_masked = mask_comments(text)
        self.nettype_none = bool(re.search(r"(?m)^\s*`default_nettype\s+none", comments_masked))
        self.masked = mask_directives(comments_masked)
        self.toks = tokenize(self.masked)
        self.newlines = [m.start() for m in re.finditer("\n", text)]
        self.pair = self._pairs()
        self.modules = self._modules()
        self.enums = {}

    # -- helpers
    def line(self, tok_index):
        return bisect.bisect_left(self.newlines, self.toks[tok_index].start) + 1

    def line_of_offset(self, offset):
        return bisect.bisect_left(self.newlines, offset) + 1

    def t(self, i):
        return self.toks[i].text if 0 <= i < len(self.toks) else ""

    def span(self, a, b):
        """Characters from the first token a to the end of token b."""
        return self.toks[a].start, self.toks[b].end

    def joined(self, a, b):
        return "".join(self.toks[k].text for k in range(a, b + 1))

    def _pairs(self):
        pair, stack = {}, []
        closers = {")": "(", "]": "[", "}": "{"}
        for i, tok in enumerate(self.toks):
            if tok.text in ("(", "[", "{", "'{"):
                stack.append(i)
            elif tok.text in closers:
                while stack and self.toks[stack[-1]].text not in ("(", "[", "{", "'{"):
                    stack.pop()
                if stack:
                    o = stack.pop()
                    pair[o] = i
                    pair[i] = o
        return pair

    def skip(self, i):
        """The index after the token at i, or after the whole bracket group that opens at i."""
        if self.t(i) in ("(", "[", "{", "'{") and i in self.pair:
            return self.pair[i] + 1
        return i + 1

    def find_depth0(self, i, stop):
        """First index >= i whose text is in `stop` outside any bracket group; raises when none before the end."""
        n = len(self.toks)
        while i < n:
            if self.t(i) in stop:
                return i
            i = self.skip(i)
        raise ParseError("unterminated")

    # -- modules
    def _modules(self):
        mods, toks, i, n = [], self.toks, 0, len(self.toks)
        while i < n:
            if toks[i].text == "module" and i + 1 < n and toks[i + 1].kind == "id":
                j = i + 1
                if toks[j].text in ("automatic", "static"):
                    j += 1
                name = toks[j].text
                j += 1
                while j < n and toks[j].text == "import":
                    j = self.find_depth0(j, {";"}) + 1
                if self.t(j) == "#" and self.t(j + 1) == "(":
                    j = self.skip(j + 1)
                port_open = None
                if self.t(j) == "(":
                    port_open = j
                    j = self.skip(j)
                try:
                    semi = self.find_depth0(j, {";"})
                    end = self.find_depth0(semi, {"endmodule"})
                except ParseError:
                    i += 1
                    continue
                module = Module(name, i, end, semi + 1)
                if port_open is not None:
                    self._ports(module, port_open + 1, self.pair.get(port_open, port_open + 1) - 1)
                self._decls(module)
                self._blocks(module)
                mods.append(module)
                i = end
            i += 1
        return mods

    def _ports(self, module, a, b):
        direction, k = "", a
        while k <= b:
            e = k
            while e <= b and self.t(e) != ",":
                e = self.skip(e)
            item = [x for x in range(k, e)]
            k = e + 1
            if not item:
                continue
            texts = [self.t(x) for x in item]
            if texts[0] in ("input", "output", "inout"):
                direction = texts[0]
            if "=" in texts:
                cut = texts.index("=")
                item, texts = item[:cut], texts[:cut]
            if not item:
                continue
            # the name is the last identifier outside brackets
            where = {x: p for p, x in enumerate(item)}
            end = len(item)
            unpacked = False
            while end and texts[end - 1] == "]" and item[end - 1] in self.pair and self.pair[item[end - 1]] in where:
                unpacked = True
                end = where[self.pair[item[end - 1]]]
            if end <= 0 or self.toks[item[end - 1]].kind != "id":
                continue
            name_i = item[end - 1]
            name = self.toks[name_i].text
            typ = [x for x in item[:end - 1]]
            decl = self._decl_from(name, "port", direction, typ, unpacked, name_i)
            module.decls[name].append(decl)
            module.ports.append((name, direction))

    def _decl_from(self, name, kind, direction, type_idx, unpacked, tok):
        typekw = typename = ""
        packed, signed = [], False
        k = 0
        while k < len(type_idx):
            x = type_idx[k]
            text = self.t(x)
            if text in ("input", "output", "inout", "var", "const", "automatic", "static"):
                pass
            elif text in TYPE_KW:
                typekw = typekw or text
            elif text in ("signed", "unsigned"):
                signed = text == "signed"
            elif text == "[" and x in self.pair:
                packed.append(self.joined(x + 1, self.pair[x] - 1).replace(" ", ""))
                while k < len(type_idx) and type_idx[k] != self.pair[x]:
                    k += 1
            elif self.toks[x].kind == "id" and text not in NOT_A_TYPE:
                typename = text if not typename else typename
            k += 1
        # a package-qualified type keeps only its last component
        return Decl(name, kind, direction, typekw, typename, tuple(packed), signed, unpacked, tok)

    def try_decl(self, i, end=None):
        """If a declaration statement starts at token i, returns ([Decl], index after the ';'), else None."""
        text = self.t(i)
        if self.toks[i].kind != "id":
            return None
        j = i
        if text in ("var", "const", "automatic", "static"):
            j += 1
            text = self.t(j)
        if text in TYPE_KW:
            j += 1
        elif text in NOT_A_TYPE or self.toks[j].kind != "id":
            return None
        else:
            # user type: ident [:: ident]* ident then one of ; , [ =
            k = j + 1
            while self.t(k) == "::" and self.toks[k + 1].kind == "id" if k + 1 < len(self.toks) else False:
                k += 2
            if not (k < len(self.toks) and self.toks[k].kind == "id" and self.t(k) not in NOT_A_TYPE
                    and self.t(k + 1) in (";", ",", "[", "=")):
                return None
            j = k
            type_idx = list(range(i, k))
            return self._decl_names(type_idx, j)
        type_idx = list(range(i, j))
        while self.t(j) in ("signed", "unsigned") or self.t(j) == "[":
            if self.t(j) == "[":
                if j not in self.pair:
                    return None
                type_idx.extend(range(j, self.pair[j] + 1))
                j = self.pair[j] + 1
            else:
                type_idx.append(j)
                j += 1
        if j >= len(self.toks) or self.toks[j].kind != "id":
            return None
        return self._decl_names(type_idx, j)

    def _decl_names(self, type_idx, j):
        decls = []
        try:
            semi = self.find_depth0(j, {";"})
        except ParseError:
            return None
        while j < semi:
            if self.toks[j].kind != "id":
                return None
            name_i = j
            j += 1
            unpacked = False
            while self.t(j) == "[" and j in self.pair:
                unpacked = True
                j = self.pair[j] + 1
            if self.t(j) == "=":
                j = self.find_depth0(j, {",", ";"})
            d = self._decl_from(self.toks[name_i].text, "local", "", type_idx, unpacked, name_i)
            d.kw = next((x for x in type_idx if self.t(x) in TYPE_KW), -1)
            decls.append(d)
            if self.t(j) == ",":
                j += 1
            elif self.t(j) != ";":
                return None
        for d in decls:
            d.names = len(decls)
        return decls, semi + 1

    def _decls(self, module):
        i, end = module.body, module.end
        while i < end:
            prev = self.t(i - 1)
            if prev in SEPARATORS or i == module.body:
                got = self.try_decl(i)
                if got:
                    for d in got[0]:
                        module.decls[d.name].append(d)
                    i = got[1]
                    continue
            i += 1

    # -- statements
    def parse_stmt(self, i):
        toks = self.toks
        if i >= len(toks):
            raise ParseError("eof")
        text = self.t(i)
        if text in ("unique", "priority", "unique0") and self.t(i + 1) in ("if", "case", "casez", "casex"):
            node, j = self.parse_stmt(i + 1)
            node.s, node.mod = i, text
            return node, j
        if text == ";":
            return types.SimpleNamespace(kind="empty", s=i, e=i), i + 1
        if text == "begin":
            j = i + 1
            if self.t(j) == ":":
                j += 2
            kids = []
            while self.t(j) != "end":
                if j >= len(toks):
                    raise ParseError("begin without end")
                got = self.try_decl(j)
                if got:
                    kids.append(types.SimpleNamespace(kind="decl", s=j, e=got[1] - 1, decls=got[0]))
                    j = got[1]
                    continue
                kid, j = self.parse_stmt(j)
                kids.append(kid)
            end = j
            j += 1
            if self.t(j) == ":":
                j += 2
            return types.SimpleNamespace(kind="block", s=i, e=end, kids=kids, mod=""), j
        if text == "if":
            if self.t(i + 1) != "(" or i + 1 not in self.pair:
                raise ParseError("if")
            close = self.pair[i + 1]
            then, j = self.parse_stmt(close + 1)
            els = None
            if self.t(j) == "else":
                els, j = self.parse_stmt(j + 1)
            return types.SimpleNamespace(kind="if", s=i, e=(els or then).e, cond=(i + 1, close), then=then, els=els, mod=""), j
        if text in ("case", "casez", "casex"):
            if self.t(i + 1) != "(" or i + 1 not in self.pair:
                raise ParseError("case")
            close = self.pair[i + 1]
            j = close + 1
            if self.t(j) == "inside":
                j += 1
            items = []
            while self.t(j) != "endcase":
                if j >= len(toks):
                    raise ParseError("case without endcase")
                start = j
                if self.t(j) == "default":
                    j += 1
                    if self.t(j) == ":":
                        j += 1
                    labels, default = [], True
                else:
                    colon = self.find_depth0(j, {":"})
                    labels, default = [], False
                    k = j
                    while k < colon:
                        e = k
                        while e < colon and self.t(e) != ",":
                            e = self.skip(e)
                        labels.append((k, e - 1))
                        k = e + 1
                    j = colon + 1
                stmt, j = self.parse_stmt(j)
                items.append(types.SimpleNamespace(kind="item", s=start, e=stmt.e, labels=labels, default=default, stmt=stmt))
            return types.SimpleNamespace(kind="case", s=i, e=j, kw=text, expr=(i + 1, close), items=items, mod=""), j + 1
        if text in ("for", "foreach", "while", "repeat"):
            if self.t(i + 1) != "(" or i + 1 not in self.pair:
                raise ParseError("loop")
            close = self.pair[i + 1]
            body, j = self.parse_stmt(close + 1)
            return types.SimpleNamespace(kind="loop", s=i, e=body.e, head=(i + 1, close), body=body), j
        if text == "forever":
            body, j = self.parse_stmt(i + 1)
            return types.SimpleNamespace(kind="loop", s=i, e=body.e, head=(i, i), body=body), j
        if text in ("else", "end", "endcase", "do"):
            raise ParseError(text)
        semi = self.find_depth0(i, {";"})
        k = i
        while k < semi:
            if self.t(k) in ASSIGN_OPS:
                lhs = (i, k - 1)
                ids = {self.t(x) for x in range(i, k) if self.toks[x].kind == "id"}
                whole = self.t(i) if k - 1 == i and self.toks[i].kind == "id" else None
                return types.SimpleNamespace(kind="assign", s=i, e=semi, lhs=lhs, op=k, opt=self.t(k), rhs=(k + 1, semi - 1),
                                             whole=whole, ids=ids, blocking=self.t(k) != "<="), semi + 1
            k = self.skip(k)
        return types.SimpleNamespace(kind="other", s=i, e=semi), semi + 1

    def _blocks(self, module):
        i, end = module.body, module.end
        while i < end:
            text = self.t(i)
            if text in ("always_comb", "always_ff", "always_latch", "always") and self.toks[i].kind == "id":
                j = i + 1
                sens = None
                try:
                    if self.t(j) == "@":
                        if self.t(j + 1) == "(" and j + 1 in self.pair:
                            sens = (j + 2, self.pair[j + 1] - 1)
                            j = self.pair[j + 1] + 1
                        else:
                            j += 2
                    body, after = self.parse_stmt(j)
                    module.always.append(types.SimpleNamespace(kind=text, sens=sens, body=body, s=i, e=body.e))
                    i = after
                    continue
                except (ParseError, IndexError):
                    pass
            elif text == "assign" and self.toks[i].kind == "id":
                j = i + 1
                if self.toks[j].kind == "id" and self.t(j + 1) == "=":
                    try:
                        semi = self.find_depth0(j + 2, {";"})
                        module.cassigns.append(types.SimpleNamespace(s=i, lhs=j, name=self.t(j), op=j + 1, rhs=(j + 2, semi - 1), semi=semi))
                    except ParseError:
                        pass
            i += 1

    def instances(self, module, known):
        """Instances of modules named in `known` inside the module: [(module name, instance name, port connections)]."""
        found = []
        toks, n = self.toks, len(self.toks)
        i = module.body
        while i < module.end:
            if toks[i].kind == "id" and toks[i].text in known and toks[i].text not in NOT_A_TYPE:
                j = i + 1
                if self.t(j) == "#" and self.t(j + 1) == "(":
                    j = self.skip(j + 1)
                if j < n and toks[j].kind == "id" and toks[j].text not in NOT_A_TYPE:
                    inst = toks[j].text
                    j += 1
                    while self.t(j) == "[" and j in self.pair:
                        j = self.pair[j] + 1
                    if self.t(j) == "(" and j in self.pair:
                        close = self.pair[j]
                        conns, k = [], j + 1
                        while k < close:
                            if self.t(k) == "." and toks[k + 1].kind == "id" and self.t(k + 2) == "(" and k + 2 in self.pair:
                                conns.append((self.t(k + 1), k + 2, self.pair[k + 2]))
                                k = self.pair[k + 2] + 1
                            else:
                                k = self.skip(k)
                        found.append(types.SimpleNamespace(module=toks[i].text, name=inst, conns=conns, tok=i))
                        i = close
            i += 1
        return found


def walk(node):
    yield node
    kind = node.kind
    if kind == "block":
        for k in node.kids:
            yield from walk(k)
    elif kind == "if":
        yield from walk(node.then)
        if node.els:
            yield from walk(node.els)
    elif kind == "case":
        for it in node.items:
            yield from walk(it.stmt)
    elif kind == "loop":
        yield from walk(node.body)


def assigns_of(node, name=None):
    return [n for n in walk(node) if n.kind == "assign" and (name is None or name in n.ids)]


def definite(node, name):
    """Whether every path through the statement assigns the whole variable: True, False (a path leaves it unassigned), or
    None (undecided: a case without default that may or may not be exhaustive, a loop). Selected writes never count."""
    kind = node.kind
    if kind == "assign":
        return node.whole == name
    if kind == "block":
        results = [definite(k, name) for k in node.kids]
        return True if any(r is True for r in results) else None if any(r is None for r in results) else False
    if kind == "if":
        if node.els is None:
            return False
        a, b = definite(node.then, name), definite(node.els, name)
        return False if a is False or b is False else True if a is True and b is True else None
    if kind == "case":
        results = [definite(it.stmt, name) for it in node.items]
        if any(r is False for r in results):
            return False
        has_default = any(it.default for it in node.items)
        return True if has_default and all(r is True for r in results) else None
    if kind == "loop":
        return None if assigns_of(node.body, name) else False
    return False


# ----------------------------------------------------------------------------------------------------------------------
# Mutation operators
# ----------------------------------------------------------------------------------------------------------------------

@dataclasses.dataclass
class Site:
    op: str
    cls: str
    module: str
    line: int
    lines: tuple
    signals: tuple
    edits: list            # (start, end, new text) in characters, applied right to left
    note: str = ""


def apply_edits(text, edits):
    out = text
    for a, b, new in sorted(edits, reverse=True):
        out = out[:a] + new + out[b:]
    if out.count("\n") != text.count("\n"):
        raise ValueError("a mutation must not add or remove a line")
    return out


def blank_span(src, a, b):
    """Edit that blanks the characters of tokens a..b (newlines stay)."""
    s, e = src.span(a, b)
    return (s, e, _blank(src.text[s:e]))


def insert(offset, text):
    return (offset, offset, text)


def site(src, module, op, cls, edits, tok, signals=(), extra_lines=(), note=""):
    line = src.line(tok)
    return Site(op, cls, module.name, line, tuple(sorted({line, *extra_lines})), tuple(signals), edits, note)


def sole_driver_names(src, module):
    """Local names whose only write in the module is one continuous assignment `assign x = ...;` and that are read."""
    names = {}
    for a in module.cassigns:
        names.setdefault(a.name, []).append(a)
    result = []
    for name, assigns in names.items():
        decl = module.decl(name)
        if len(assigns) != 1 or decl is None or decl.kind != "local" or decl.unpacked or not (decl.typekw in ("logic", "wire", "reg", "bit")):
            continue
        a = assigns[0]
        reads, bad = 0, False
        for i in range(module.body, module.end):
            if src.t(i) != name or src.toks[i].kind != "id":
                continue
            if i == decl.tok or i == a.lhs:
                continue
            prev = src.t(i - 1)
            if prev in (".", "::"):
                bad = True
                break
            # any write context other than the continuous assignment
            j = i + 1
            while src.t(j) == "[" and j in src.pair:
                j = src.pair[j] + 1
            if src.t(j) in ASSIGN_OPS and (src.t(j) != "<=" or _is_nb_statement_start(src, i)):
                bad = True
                break
            if src.t(j) in ("++", "--") or prev in ("++", "--") or _in_named_connection(src, i, module.body):
                bad = True       # another write, or a named port connection that may drive it
                break
            if _in_lhs_concat(src, i):
                bad = True
                break
            reads += 1
        if bad or reads == 0:
            continue
        # unnamed connections (positional) could also drive it: reject when the name appears inside an instance list
        result.append((name, a, decl))
    return result


def _in_named_connection(src, i, floor):
    """Whether the token at i sits inside a `.port( ... )` connection."""
    depth, k = 0, i
    while k > floor:
        k -= 1
        text = src.t(k)
        if text in (")", "]", "}"):
            depth += 1
        elif text in ("(", "[", "{", "'{"):
            if depth:
                depth -= 1
            elif text == "(" and src.toks[k - 1].kind == "id" and src.t(k - 2) == ".":
                return True
        elif text == ";" and depth == 0:
            return False
    return False


def _is_nb_statement_start(src, i):
    """Whether the identifier at i begins a statement (so a following <= is an assignment, not a comparison)."""
    prev = src.t(i - 1)
    return prev in (";", "begin", "end", ")", "else") or prev == ":"


def _in_lhs_concat(src, i):
    """Whether the identifier at i sits in a `{...} =` / `{...} <=` left-hand side."""
    depth = 0
    k = i
    while k > 0 and depth >= 0:
        k -= 1
        text = src.t(k)
        if text == "}":
            depth += 1
        elif text == "{":
            if depth == 0:
                close = src.pair.get(k)
                return close is not None and src.t(close + 1) in ASSIGN_OPS
            depth -= 1
        elif text in (";", "begin", "end") and depth == 0:
            return False
    return False


def observable(src, module, name):
    """Whether a continuously assigned name matters outside the module: an output, or read somewhere after its one
    declaration and its one assignment."""
    d = module.decl(name)
    if d is None:
        return False
    if d.kind == "port":
        return d.direction != "input"
    uses = sum(1 for i in range(module.body, module.end) if src.toks[i].kind == "id" and src.toks[i].text == name)
    return uses >= 3


def op_lc02(src, module):
    out = []
    for a in module.cassigns:
        lhs, rhs = module.decl(a.name), None
        if a.rhs[0] != a.rhs[1] or src.toks[a.rhs[0]].kind != "id":
            continue
        rhs_name = src.t(a.rhs[0])
        rhs = module.decl(rhs_name)
        if not lhs or not rhs or lhs.unpacked or rhs.unpacked or lhs.typename or rhs.typename or not observable(src, module, a.name):
            continue
        if not lhs.typekw or not rhs.typekw or lhs.packed != rhs.packed or len(lhs.packed) > 1 or lhs.signed != rhs.signed:
            continue
        rt = a.rhs[0]
        out.append(site(src, module, "LC02-widen-truncate", "LC02",
                        [(src.toks[rt].start, src.toks[rt].end, f"{{{rhs_name}, {rhs_name}}}")], a.s, (a.name, rhs_name)))
    return out


def op_lc04(src, module):
    out = []
    for name, decls in module.decls.items():
        d = module.decl(name)
        if not d or d.kind != "local" and d.kind != "port" or d.unpacked or d.typename or d.typekw not in ("logic", "wire", "reg", "bit"):
            continue
        if len(d.packed) != 1:
            continue
        hi = d.packed[0].split(":")
        if len(hi) != 2 or hi[1] != "0":
            continue
        top = hi[0]
        if re.fullmatch(r"\d+", top):
            limit = str(int(top) + 1)
        elif top.endswith("-1") and re.fullmatch(r"[\w$]+", top[:-2]):
            limit = top[:-2]
        else:
            continue
        for i in range(module.body, module.end):
            if src.t(i) != name or src.toks[i].kind != "id" or src.t(i + 1) != "[" or src.t(i - 1) in (".", "::"):
                continue
            close = src.pair.get(i + 1)
            if close is None or close == i + 2:
                continue
            inner = range(i + 2, close)
            if any(src.t(k) in (":", "+:", "-:", "[", "]") for k in inner) or i == d.tok:
                continue
            if module.decls[name][0].tok >= i:
                continue
            a, b = src.span(i + 2, close - 1)
            out.append(site(src, module, "LC04-index-past-end", "LC04", [(a, b, limit)], i, (name,)))
    return out


def op_lc05(src, module, known):
    out = []
    if src.nettype_none:
        return out
    for inst in src.instances(module, known):
        for port, o, c in inst.conns:
            if c == o + 2 and src.toks[o + 1].kind == "id" and module.decl(src.t(o + 1)):
                name = src.t(o + 1)
                typo = name + "_tpo"
                if typo in module.decls:
                    continue
                t = src.toks[o + 1]
                out.append(site(src, module, "LC05-typo-net", "LC05", [(t.start, t.end, typo)], o + 1, (name, typo)))
    return out


def op_lc06(src, module):
    out = []
    for name, a, decl in sole_driver_names(src, module):
        s, e = src.span(a.s, a.semi)
        out.append(site(src, module, "LC06-delete-driver", "LC06", [(s, e, _blank(src.text[s:e]))], a.s, (name,),
                        extra_lines=(src.line(a.semi),)))
    return out


def op_lc07(src, module):
    out = []
    for name, a, decl in sole_driver_names(src, module):
        if a.rhs[0] > a.rhs[1]:
            continue
        rhs = re.sub(r"\s+", " ", src.text[src.toks[a.rhs[0]].start:src.toks[a.rhs[1]].end])
        edits = []
        if decl.typekw != "wire":
            # `logic x;` becomes `wire x;` so that a second continuous driver elaborates
            if decl.typekw not in ("logic", "reg") or decl.kw < 0 or decl.names != 1:
                continue
            t = src.toks[decl.kw]
            edits.append((t.start, t.end, "wire"))
        edits.append(insert(src.toks[a.semi].end, f" assign {name} = ~({rhs});"))
        out.append(site(src, module, "LC07-second-driver", "LC07", edits, a.s, (name,)))
    return out


def op_lc08(src, module, known, ports):
    out = []
    for inst in src.instances(module, known):
        want = ports.get(inst.module, {})
        for port, o, c in inst.conns:
            if want.get(port) == "input" and c > o + 1:
                s, e = src.span(o + 1, c - 1)
                names = tuple(src.t(k) for k in range(o + 1, c) if src.toks[k].kind == "id")
                out.append(site(src, module, "LC08-empty-input", "LC08", [(s, e, _blank(src.text[s:e]))], o, names + (port,),
                                extra_lines=(src.line(c),)))
    return out


def op_lc10(src, module):
    out = []
    for a in module.cassigns:
        d = module.decl(a.name)
        if not d or d.unpacked or d.typename or d.typekw not in ("logic", "wire", "reg", "bit") or a.rhs[0] > a.rhs[1]:
            continue
        if len([x for x in module.cassigns if x.name == a.name]) != 1 or not observable(src, module, a.name):
            continue
        r0, r1 = src.toks[a.rhs[0]], src.toks[a.rhs[1]]
        semi = src.toks[a.semi]
        out.append(site(src, module, "LC10-self-xor", "LC10", [insert(r0.start, "("), insert(semi.start, f") ^ {a.name}")], a.s,
                        (a.name,), extra_lines=(src.line(a.semi),)))
    return out


def comb_blocks(module):
    return [b for b in module.always if b.kind == "always_comb"]


def op_lc11(src, module):
    out = []
    for blk in comb_blocks(module):
        body = blk.body
        if body.kind != "block":
            continue
        local = {d.name for k in body.kids if k.kind == "decl" for d in k.decls}
        for idx, kid in enumerate(body.kids):
            if kid.kind != "assign" or kid.opt != "=" or not kid.whole or kid.rhs[0] > kid.rhs[1] or kid.whole in local:
                continue
            name = kid.whole
            if module.decl(name) is None:
                continue
            following = [k for k in body.kids[idx + 1:] if k.kind in ("if", "case")]
            if not following:
                continue
            writes = assigns_of(body, name)
            if any(w.whole != name for w in writes):
                continue                       # a selected or concatenated write: coverage cannot be proved
            rest = types.SimpleNamespace(kind="block", s=body.s, e=body.e, kids=[k for k in body.kids if k is not kid], mod="")
            if definite(rest, name) is not False:
                continue
            if not any(assigns_of(f, name) for f in following):
                continue
            out.append(site(src, module, "LC11-delete-default", "LC11", [blank_span(src, kid.s, kid.e)], kid.s, (name,),
                            extra_lines=(src.line(blk.s),)))
    return out


def op_lc12(src, module):
    out = []
    for blk in module.always:
        if blk.kind == "always_ff":
            nodes = [n for n in walk(blk.body) if n.kind == "assign" and n.opt == "<=" and n.whole]
            for n in nodes:
                later = [m for m in nodes if m is not n and m.s > n.e and any(
                    src.t(k) == n.whole and src.toks[k].kind == "id" for k in range(m.rhs[0], m.rhs[1] + 1))]
                if not later:
                    continue
                out.append(site(src, module, "LC12-nonblocking-to-blocking", "LC12", [(src.toks[n.op].start, src.toks[n.op].end, "=")],
                                n.s, (n.whole,)))
        elif blk.kind == "always_comb":
            for n in walk(blk.body):
                if n.kind != "assign" or n.opt != "=" or n.rhs[0] > n.rhs[1]:
                    continue
                name = next((src.t(k) for k in range(n.lhs[0], n.lhs[1] + 1) if src.toks[k].kind == "id"), None)
                d = module.decl(name) if name else None
                if not d or d.typekw in ("int", "integer", "byte", "shortint", "longint") or (not d.typekw and not d.typename):
                    continue
                if any(name in {x.name for x in k.decls} for k in walk(blk.body) if k.kind == "decl"):
                    continue
                if inside_loop_head(blk.body, n):
                    continue
                out.append(site(src, module, "LC12-blocking-to-nonblocking", "LC12", [(src.toks[n.op].start, src.toks[n.op].end, "<=")],
                                n.s, (name,)))
    return out


def inside_loop_head(root, node):
    return any(k.kind == "loop" and k.head[0] <= node.s <= k.head[1] for k in walk(root))


def enum_table(sources):
    """{enum typedef name: [enumerators]} over every parsed source (typedef enum ... { A, B } name_t;)."""
    table = {}
    for src in sources:
        for m in re.finditer(r"typedef\s+enum\b[^{;]*\{([^}]*)\}\s*(\w+)\s*;", src.masked):
            names = [re.sub(r"\s*=.*", "", x).strip() for x in m.group(1).split(",")]
            table[m.group(2)] = [x for x in names if x]
    return table


def case_uncovered(src, module, case, enums, ignore_default=True):
    """True when the case certainly leaves some value of its expression unhandled once its default is gone: a plain
    identifier over an enum with an enumerator that is no label, or over sized constants that are fewer than the values."""
    if case.kw != "case":
        return False
    a, b = case.expr[0] + 1, case.expr[1] - 1
    if a != b or src.toks[a].kind != "id":
        return False
    d = module.decl(src.t(a))
    labels = []
    for it in case.items:
        if it.default:
            continue
        for (x, y) in it.labels:
            labels.append([src.t(k) for k in range(x, y + 1)])
    if not labels:
        return False
    if d and d.typename and d.typename in enums:
        wanted = set(enums[d.typename])
        got = set()
        for lab in labels:
            if len(lab) == 1 and lab[0] in wanted:
                got.add(lab[0])
            elif len(lab) == 3 and lab[1] == "::" and lab[2] in wanted:
                got.add(lab[2])
            else:
                return False
        return len(got) < len(wanted)
    sizes, values = set(), set()
    for lab in labels:
        m = re.fullmatch(r"(\d+)'[bBhHdDoO]([0-9a-fA-F_]+)", "".join(lab))
        if len(lab) != 1 or not m:
            return False
        base = {"b": 2, "h": 16, "d": 10, "o": 8}[lab[0][lab[0].index("'") + 1].lower()]
        try:
            values.add(int(m.group(2).replace("_", ""), base))
        except ValueError:
            return False
        sizes.add(int(m.group(1)))
    return len(sizes) == 1 and next(iter(sizes)) <= 16 and len(values) < 2 ** next(iter(sizes))


def op_lc13(src, module, enums):
    out = []
    for blk in comb_blocks(module):
        for case in walk(blk.body):
            if case.kind != "case":
                continue
            default = next((it for it in case.items if it.default), None)
            if default is None or not case_uncovered(src, module, case, enums):
                continue
            name = src.t(case.expr[0] + 1)
            out.append(site(src, module, "LC13-delete-case-default", "LC13", [blank_span(src, default.s, default.e)], default.s, (name,),
                            extra_lines=(src.line(case.s),)))
    return out


def op_lc14(src, module):
    out = []
    for blk in comb_blocks(module):
        for n in walk(blk.body):
            if n.kind != "assign" or n.opt != "=" or n.rhs[0] != n.rhs[1]:
                continue
            tok = src.toks[n.rhs[0]]
            if tok.kind != "num" or not re.search(r"'", tok.text) or re.search(r"[xXzZ?]", tok.text.split("'")[-1]):
                continue
            name = next((src.t(k) for k in range(n.lhs[0], n.lhs[1] + 1) if src.toks[k].kind == "id"), None)
            if not name or module.decl(name) is None or inside_loop_head(blk.body, n):
                continue
            out.append(site(src, module, "LC14-constant-to-x", "LC14", [(tok.start, tok.end, "'x")], n.s, (name,)))
    return out


def op_lc15(src, module):
    out = []
    for a in module.cassigns:
        if src.t(a.s + 1) == "#":
            continue
        t = src.toks[a.s]
        out.append(site(src, module, "LC15-delay", "LC15", [insert(t.end, " #1")], a.s, (a.name,)))
    return out


def is_const_rhs(src, a, b):
    if a == b:
        tok = src.toks[a]
        return tok.kind == "num" and "'" in tok.text or (tok.kind == "id" and re.fullmatch(r"[A-Z][A-Za-z0-9_]*", tok.text) is not None)
    return b == a + 2 and src.t(a + 1) == "::" and re.fullmatch(r"[A-Z][A-Za-z0-9_]*", src.t(b)) is not None


def op_lc16(src, module):
    out = []
    for blk in module.always:
        if blk.kind not in ("always_ff", "always") or not blk.sens or blk.body.kind != "block" or not blk.body.kids:
            continue
        sens = [src.t(k) for k in range(blk.sens[0], blk.sens[1] + 1)]
        if "or" not in sens and "," not in sens:
            continue
        edge = [i for i, w in enumerate(sens) if w in ("negedge", "posedge")]
        if len(edge) < 2:
            continue
        reset = sens[edge[1] + 1] if edge[1] + 1 < len(sens) else ""
        first = next((k for k in blk.body.kids if k.kind != "decl"), None)
        if first is None or first.kind != "if" or reset not in {src.t(k) for k in range(first.cond[0], first.cond[1] + 1)}:
            continue
        for n in assigns_of(first.then):
            d = module.decl(n.whole) if n.whole else None
            if not d or n.opt != "<=" or d.unpacked or not d.typekw or d.typename or not is_const_rhs(src, *n.rhs):
                continue
            cands = [x for x in module.decls.values() if len(x) == 1 and x[0].name != n.whole and x[0].packed == d.packed
                     and x[0].typekw == d.typekw and not x[0].unpacked and not x[0].typename and x[0].signed == d.signed
                     and x[0].direction == "input" and not re.match(r"(?i)(clk|rst)", x[0].name) and not x[0].name.startswith("scan")]
            if not cands:
                continue
            pick = sorted(cands, key=lambda x: x[0].name)[0][0].name
            a, b = src.span(*n.rhs)
            out.append(site(src, module, "LC16-reset-value-from-data", "LC16", [(a, b, pick)], n.s, (n.whole, pick)))
    return out


def op_lc17(src, module):
    clocks = [name for name, dr in module.ports if dr == "input" and re.fullmatch(r"clk\w*", name) and module.decl(name)
              and not module.decl(name).packed and not module.decl(name).unpacked]
    out = []
    if not clocks:
        return out
    clk = clocks[0]
    for a in module.cassigns:
        d = module.decl(a.name)
        if not d or d.packed or d.unpacked or d.typename or d.typekw not in ("logic", "wire") or a.rhs[0] > a.rhs[1]:
            continue
        if len([x for x in module.cassigns if x.name == a.name]) != 1 or a.name == clk or not observable(src, module, a.name):
            continue
        r0, semi = src.toks[a.rhs[0]], src.toks[a.semi]
        out.append(site(src, module, "LC17-clock-as-data", "LC17", [insert(r0.start, "("), insert(semi.start, f") & {clk}")], a.s,
                        (a.name, clk)))
    return out


def op_lc21(src, module):
    guards = [d[0] for d in module.decls.values() if len(d) == 1 and d[0].typekw in ("logic", "wire", "reg") and d[0].packed
              and not d[0].unpacked and not d[0].signed and not d[0].typename and d[0].direction in ("input", "")
              and not d[0].name.startswith("clk")]
    out = []
    for blk in module.always:
        for parent in walk(blk.body):
            if parent.kind != "block":
                continue
            for n in parent.kids:
                if n.kind != "assign" or n.opt not in ("=", "<=") or n.rhs[0] > n.rhs[1] or inside_loop_head(blk.body, n):
                    continue
                # a guard declared before the statement: an input port, else a local
                usable = [d for d in guards if d.tok < n.s]
                if not usable:
                    continue
                guard = sorted(usable, key=lambda d: (d.direction != "input", d.name))[0].name
                out.append(site(src, module, "LC21-constant-comparison", "LC21", [insert(src.toks[n.s].start, f"if ({guard} < 0) ")],
                                n.s, (guard,), extra_lines=(src.line(n.e),)))
    return out


def op_lc25(src, module, enums):
    out = []
    for blk in module.always:
        for case in walk(blk.body):
            if case.kind == "case" and not any(it.default for it in case.items) and case_uncovered(src, module, case, enums):
                close = src.toks[case.expr[1]]
                rest = src.text[close.end:src.text.find("\n", close.end) if src.text.find("\n", close.end) != -1 else len(src.text)]
                text = " // synopsys full_case" if not rest.strip() else " /* synopsys full_case */"
                out.append(site(src, module, "LC25-full-case", "LC25", [insert(close.end, text)], case.s, (src.t(case.expr[0] + 1),)))
        if blk.kind in ("always_comb", "always_ff"):
            for n in walk(blk.body):
                if n.kind == "assign" and n.rhs[0] <= n.rhs[1] and n.whole:
                    a, b = src.toks[n.s].start, src.toks[n.e].end
                    out.append(site(src, module, "LC25-translate-off", "LC25",
                                    [insert(a, "/* synopsys translate_off */ "), insert(b, " /* synopsys translate_on */")], n.s, (n.whole,),
                                    extra_lines=(src.line(n.e),)))
    return out


OPERATORS = ["LC02-widen-truncate", "LC04-index-past-end", "LC05-typo-net", "LC06-delete-driver", "LC07-second-driver",
             "LC08-empty-input", "LC10-self-xor", "LC11-delete-default", "LC12-nonblocking-to-blocking",
             "LC12-blocking-to-nonblocking", "LC13-delete-case-default", "LC14-constant-to-x", "LC15-delay",
             "LC16-reset-value-from-data", "LC17-clock-as-data", "LC21-constant-comparison", "LC25-full-case",
             "LC25-translate-off"]


def candidate_sites(src, module, known, ports, enums):
    sites = []
    sites += op_lc02(src, module) + op_lc04(src, module) + op_lc05(src, module, known) + op_lc06(src, module)
    sites += op_lc07(src, module) + op_lc08(src, module, known, ports) + op_lc10(src, module) + op_lc11(src, module)
    sites += op_lc12(src, module) + op_lc13(src, module, enums) + op_lc14(src, module) + op_lc15(src, module)
    sites += op_lc16(src, module) + op_lc17(src, module) + op_lc21(src, module) + op_lc25(src, module, enums)
    return sites


# ----------------------------------------------------------------------------------------------------------------------
# Scoring
# ----------------------------------------------------------------------------------------------------------------------

def wilson(k, n, z=1.96):
    if n == 0:
        return (0.0, 1.0)
    p = k / n
    d = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / d
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (max(0.0, centre - half), min(1.0, centre + half))


def file_key(path):
    """Path of a staged source below its `src/` directory (the same for the original and a mutated copy)."""
    path = str(path).replace("\\", "/")
    k = path.rfind("/src/")
    return path[k + 5:] if k != -1 else path


def gating(d):
    disposition = d.get("disposition", "")
    if disposition in ("WAIVED", "RESOLVED", "OBLIGATION", "NOT_ANALYSED", "BASELINED"):
        return False
    if d["severity"] in ("ERROR", "FATAL"):
        return True
    return d["severity"] == "WARNING" and d.get("confidence") in ("established", "heuristic")


def diag_key(d):
    loc = d.get("location") or {}
    return (d["rule_id"], file_key(loc["file"]), loc.get("line", 0)) if loc.get("file") else None


def new_findings(report, baseline):
    """Diagnostics of `report` that have no counterpart in the baseline: same rule, file and message, line within 2."""
    old = collections.defaultdict(list)
    for d in baseline["diagnostics"]:
        key = diag_key(d)
        if key:
            old[(key[0], key[1], d.get("message", ""))].append(key[2])
    fresh = []
    for d in report["diagnostics"]:
        key = diag_key(d)
        if key is None:
            continue
        if any(abs(key[2] - line) <= 2 for line in old.get((key[0], key[1], d.get("message", "")), [])):
            continue
        fresh.append(d)
    return fresh


def names_signal(d, signals):
    text = d.get("message", "") + " " + " ".join(d.get("signals") or []) + " " + (d.get("hierarchy") or "")
    return any(re.search(r"(?<![\w$])" + re.escape(s) + r"(?![\w$])", text) for s in signals if s)


_RULE_CLASSES = {}


def rule_classes(rule):
    """Defect classes the catalog lists for a rule (its `classes`)."""
    if not _RULE_CLASSES:
        catalog = json.loads((HERE.parents[1] / "docs/rules/catalog.json").read_text())
        _RULE_CLASSES.update({r["id"]: set(r.get("classes", [])) for r in catalog["rules"]})
    return _RULE_CLASSES.get(rule, set())


def score(mutant, fresh):
    """(outcome, finding) of one mutant against the new findings of its run; a gating finding of a rule that the catalog
    lists for the mutant's class is preferred, and the caller records whether the match is in class."""
    matched = []
    for d in fresh:
        loc = d.get("location") or {}
        if file_key(loc.get("file", "")) != mutant["file_key"]:
            continue
        at = any(abs(loc.get("line", -99) - line) <= 2 for line in mutant["lines"])
        if at or names_signal(d, mutant["signals"]):
            matched.append(d)
    if not matched and mutant["cls"] in SOLO:
        # a rule that reports once per clock root (LNT-027) places the finding at the instance that carries the clock, not at
        # the use, so a run of its own attributes any new finding of the class to the one mutant
        matched = [d for d in fresh if mutant["cls"] in rule_classes(d["rule_id"])]
    if not matched:
        return "MISSED", None
    gated = [d for d in matched if gating(d)]
    if gated:
        return "CAUGHT", min(gated, key=lambda d: mutant["cls"] not in rule_classes(d["rule_id"]))
    for d in matched:
        if d.get("disposition") != "WAIVED":
            return "CAUGHT_NONGATING", d
    return "CAUGHT_WAIVED", matched[0]


def frontend_errors(report):
    """Errors the frontend (slang) reported; a run with one analyses only part of the rules."""
    return [d for d in report["diagnostics"] if d["severity"] in ("ERROR", "FATAL") and d.get("origin", "") == "slang"
            and (d.get("location") or {}).get("file")]


# ----------------------------------------------------------------------------------------------------------------------
# Runs
# ----------------------------------------------------------------------------------------------------------------------

def available_gb():
    for line in pathlib.Path("/proc/meminfo").read_text().splitlines():
        if line.startswith("MemAvailable:"):
            return int(line.split()[1]) / 1024 / 1024
    return 0.0


def wait_for_memory(minimum, minutes):
    waited = 0
    while available_gb() < minimum:
        if waited >= minutes:
            raise RuntimeError(f"less than {minimum} GB available for {minutes} minutes")
        time.sleep(60)
        waited += 1


def run_sverity(binary, top, design_f, config, waivers, root, report, args, tag, extra=()):
    command = [str(binary), "--single-unit", "--goal", "lint", "--top", top, "-f", str(design_f), "--config", str(config),
               "--waivers", str(waivers), "--root", str(root), "--json", str(report), "--max-console", "1", *extra]
    if args.memory_gb and shutil.which("systemd-run"):
        unit = f"mutation-recall-{tag}-{int(time.time())}"
        command = ["systemd-run", "--user", "--wait", "--collect", "--same-dir", "-E", "PATH", "-E", "HOME", "-p", f"MemoryMax={args.memory_gb}G",
                   "-p", "MemorySwapMax=0", "-p", "RuntimeMaxSec=900", f"--unit={unit}", "-q"] + command
    elif args.memory_gb:
        command = ["bash", "-c", f"ulimit -v {int(args.memory_gb * 1024 * 1024)}; exec \"$@\"", "bash"] + command
    pathlib.Path(report).unlink(missing_ok=True)
    wait_for_memory(args.min_available_gb, args.wait_minutes)
    done = subprocess.run(command, capture_output=True, text=True)
    if not pathlib.Path(report).exists():
        return None, done
    return json.loads(pathlib.Path(report).read_text()), done


def read_design(design_f):
    lines = pathlib.Path(design_f).read_text().splitlines()
    return lines, [l for l in lines if l and not l.startswith("+")]


def own_sources(files, ip):
    """Staged RTL files of the IP's own core (not primitives, packages, generated register files)."""
    out = []
    for f in files:
        key = file_key(f)
        core, _, rest = key.partition("/")
        if not re.fullmatch(rf"lowrisc_ip_{ip}_[\d.]+", core):
            continue
        base = pathlib.Path(f).name
        if base.endswith(("_reg_top.sv", "_reg_pkg.sv")) or "_pkg" in base or not base.endswith(".sv"):
            continue
        out.append(f)
    return out


def opentitan_path(replay, file):
    """Path of a staged file in the OpenTitan checkout (matched by name and bytes), or None."""
    repo = pathlib.Path(replay) / "opentitan"
    data = pathlib.Path(file).read_bytes()
    for cand in repo.rglob(pathlib.Path(file).name):
        if cand.is_file() and cand.read_bytes() == data:
            return str(cand.relative_to(repo))
    return None


def reachable(parsed, top):
    modules = {m.name: (s, m) for s in parsed.values() for m in s.modules}
    known = set(modules)
    seen, todo = set(), [top]
    while todo:
        name = todo.pop()
        if name in seen or name not in modules:
            continue
        seen.add(name)
        src, module = modules[name]
        for inst in src.instances(module, known):
            todo.append(inst.module)
    return seen


def pick_sites(candidates, per_op, rng):
    """Per operator, up to per_op sites spread over distinct files first, in a seeded order."""
    chosen = []
    by_op = collections.defaultdict(list)
    for c in candidates:
        by_op[c["op"]].append(c)
    for op in OPERATORS:
        pool = by_op.get(op, [])
        rng.shuffle(pool)
        used = collections.Counter()
        picked = []
        while pool and len(picked) < per_op:
            pool.sort(key=lambda c: used[c["file"]])
            c = pool.pop(0)
            used[c["file"]] += 1
            picked.append(c)
        chosen += picked
    return chosen


def batches_of(mutants):
    batches = []
    for m in mutants:
        if m["cls"] in SOLO:
            batches.append([m])
            continue
        for b in batches:
            if b[0]["cls"] not in SOLO and m["file"] not in {x["file"] for x in b}:
                b.append(m)
                break
        else:
            batches.append([m])
    return batches


def run_ip(args, ip, replay, scratch, binary):
    base = pathlib.Path(replay) / "lint" / ip
    summary = json.loads((base / "summary.json").read_text())
    top = summary["top"]
    lines, files = read_design(base / "design.f")
    parsed = {f: Source(pathlib.Path(f).read_text(errors="replace")) for f in files if f.endswith((".sv", ".v"))}
    known = {m.name for s in parsed.values() for m in s.modules}
    ports = {m.name: {n: d for n, d in m.ports} for s in parsed.values() for m in s.modules}
    enums = enum_table(parsed.values())
    live = reachable(parsed, top)
    work = pathlib.Path(scratch) / "mut" / ip
    work.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(replay) / "opentitan"
    config, waivers = base / "config.json", base / "waivers.json"

    baseline_report = work / "baseline.json"
    baseline, done = run_sverity(binary, top, base / "design.f", config, waivers, root, baseline_report, args, f"{ip}-base")
    if baseline is None:
        raise RuntimeError(f"{ip}: the baseline run wrote no report: {done.stderr[-300:]}")
    base_errors = {diag_key(d) for d in frontend_errors(baseline)}
    skipped = collections.Counter()
    candidates = []
    own = own_sources(files, ip)
    for f in own:
        src = parsed[f]
        rel = opentitan_path(replay, f)
        if rel is None:
            skipped["file not found byte-identical in the checkout"] += 1
            continue
        for module in src.modules:
            if module.name not in live:
                skipped["module not elaborated from the top"] += 1
                continue
            for s in candidate_sites(src, module, known, ports, enums):
                try:
                    apply_edits(src.text, s.edits)
                except ValueError:
                    skipped["site dropped: its edit changes the line count"] += 1
                    continue
                candidates.append(dict(ip=ip, file=f, path=rel, file_key=file_key(f), op=s.op, cls=s.cls, module=s.module, line=s.line,
                                       lines=list(s.lines), signals=list(s.signals), edits=s.edits))
    rng = random.Random(f"{args.seed}:{ip}")
    for f in own:
        if parsed[f].nettype_none and any(m.name in live for m in parsed[f].modules):
            skipped["LC05 skipped: file has default_nettype none"] += 1
    chosen = pick_sites(candidates, args.per_op, rng)
    results, run_count = [], 0
    pending = batches_of(chosen)
    index = 0
    while index < len(pending):
        batch = pending[index]
        index += 1
        tag = f"b{index:02d}"
        for m in batch:
            m["run"] = f"{ip}/{tag}"
        run_dir = work / tag
        mapping = {}
        for m in batch:
            mutated = apply_edits(pathlib.Path(m["file"]).read_text(errors="replace"), m["edits"])
            key = file_key(m["file"])
            dest = run_dir / "src" / key
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(mutated)
            mapping[m["file"]] = str(dest)
        design_f = run_dir / "design.f"
        design_f.write_text("\n".join(mapping.get(l, l) for l in lines) + "\n")
        report, done = run_sverity(binary, top, design_f, config, waivers, root, run_dir / "report.json", args, f"{ip}-{tag}")
        run_count += 1
        if report is None:
            for m in batch:
                results.append(dict(m, outcome="NOT_RUN", detail="no report: " + done.stderr[-200:].replace("\n", " ")))
            continue
        keys = {m["file_key"] for m in batch}
        errors = [d for d in frontend_errors(report) if diag_key(d) not in base_errors]
        if report["summary"]["execution_status"] != "SUCCESS" and not errors:
            for m in batch:
                results.append(dict(m, outcome="NOT_RUN", detail="execution " + report["summary"]["execution_status"]))
            continue
        if any(file_key(d["location"]["file"]) not in keys for d in errors):
            # an error outside the mutated files: find which mutant causes it by running them one at a time
            if len(batch) == 1:
                d = next(d for d in errors if file_key(d["location"]["file"]) not in keys)
                results.append(dict(batch[0], outcome="INVALID", matched=None, detail=f"{d['rule_id']} elsewhere: {d['message'][:80]}"))
            else:
                pending[index:index] = [[m] for m in batch]
            continue
        erroring = []
        for m in batch:
            own_errors = [d for d in errors if file_key(d["location"]["file"]) == m["file_key"]]
            if not own_errors:
                continue
            erroring.append(m)
            in_class = [d for d in own_errors if m["cls"] in rule_classes(d["rule_id"])]
            if in_class:
                d = in_class[0]
                results.append(dict(m, outcome="CAUGHT", matched=dict(rule=d["rule_id"], severity=d["severity"], confidence=d.get("confidence"),
                                                                      disposition=d.get("disposition"), line=d["location"].get("line"), in_class=True,
                                                                      frontend_error=True)))
            else:
                d = own_errors[0]
                results.append(dict(m, outcome="INVALID", matched=None, detail=f"does not elaborate: {d['rule_id']} {d['message'][:80]}"))
        if erroring:
            # a run with a frontend error skips rules: score the others in a run of their own
            rest = [m for m in batch if m not in erroring]
            if rest:
                pending.insert(index, rest)
            continue
        fresh = new_findings(report, baseline)
        for m in batch:
            outcome, d = score(m, fresh)
            results.append(dict(m, outcome=outcome, matched=None if d is None else dict(
                rule=d["rule_id"], severity=d["severity"], confidence=d.get("confidence"), disposition=d.get("disposition"),
                line=(d.get("location") or {}).get("line"), in_class=m["cls"] in rule_classes(d["rule_id"]))))
    missed = [r for r in results if r["outcome"] == "MISSED"]
    if missed:
        live_sites, found, planted = probe_elaboration(args, binary, ip, work, parsed, files, lines, top, config, waivers, root, missed)
        for n, r in enumerate(missed):
            if not live_sites[n]:
                r["outcome"], r["matched"] = "INVALID", None
                r["detail"] = "not elaborated: the module or generate branch is never built in this configuration"
    for r in results:
        r.pop("edits", None)
        r.pop("file", None)
    return dict(ip=ip, top=top, runs=run_count + 1, baseline_open=sum(1 for d in baseline["diagnostics"] if gating(d)),
                candidates=collections.Counter(c["op"] for c in candidates), skipped=dict(skipped), mutants=results)


def probe_spots(src, module, offset):
    """Where to put probes for a site at character `offset`: after the module header and after every `begin` that encloses
    the site outside procedural code (generate blocks). Returns [(character offset after the spot, index)]."""
    toks = src.toks
    idx = bisect.bisect_right([t.start for t in toks], offset) - 1
    spots = [src.toks[module.body - 1].end]
    procedural = [(b.s, b.e) for b in module.always]
    i = module.body
    stack = []
    for k in range(module.body, min(idx + 1, module.end)):
        text = toks[k].text
        if text == "begin":
            stack.append(k)
        elif text == "end" and stack:
            stack.pop()
        elif text in ("function", "task") and toks[k].kind == "id":
            stack.append(k)
        elif text in ("endfunction", "endtask") and stack:
            stack.pop()
    for k in stack:
        if toks[k].text != "begin":
            break                                   # inside a function or task: nothing below it is a generate block
        if any(a <= k <= b for a, b in procedural):
            continue
        j = k + 1
        if src.t(j) == ":" and j + 1 < len(toks):
            j += 2
        spots.append(toks[j - 1].end)
    return sorted(set(spots))


def probe_elaboration(args, binary, ip, work, parsed, files, lines, top, config, waivers, root, missed):
    """Reruns the unmutated design with a probe (a localparam initialised from an undeclared name) at the module header and
    inside every generate block that holds each missed site. The frontend reports the undeclared name only where the
    code is elaborated, so a probe without its error marks a site in a module or generate branch that this
    configuration never builds. {mutant index: True when every enclosing scope is elaborated}."""
    plan, edits, serial = {}, collections.defaultdict(list), 0
    chosen = {}
    for n, m in enumerate(missed):
        src = parsed[m["file"]]
        offset = m["edits"][0][0]
        module = next((x for x in src.modules if x.start <= bisect.bisect_right([t.start for t in src.toks], offset) - 1 < x.end), None)
        if module is None:
            plan[n] = None
            continue
        ids = []
        for spot in probe_spots(src, module, offset):
            key = (m["file"], spot)
            if key not in chosen:
                chosen[key] = serial
                edits[m["file"]].append(insert(spot, f" localparam int __sv_probe{serial} = __sv_undeclared{serial};"))
                serial += 1
            ids.append(chosen[key])
        plan[n] = ids
    run_dir = work / "probe"
    mapping = {}
    for f, es in edits.items():
        dest = run_dir / "src" / file_key(f)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(apply_edits(pathlib.Path(f).read_text(errors="replace"), es))
        mapping[f] = str(dest)
    (run_dir / "design.f").parent.mkdir(parents=True, exist_ok=True)
    (run_dir / "design.f").write_text("\n".join(mapping.get(l, l) for l in lines) + "\n")
    report, done = run_sverity(binary, top, run_dir / "design.f", config, waivers, root, run_dir / "report.json", args, f"{ip}-probe",
                               extra=("--error-limit", "0"))
    if report is None:
        raise RuntimeError(f"{ip}: the probe run wrote no report: {done.stderr[-300:]}")
    hit = {int(x) for d in frontend_errors(report) for x in re.findall(r"__sv_undeclared(\d+)", d["message"])}
    return {n: (ids is not None and all(i in hit for i in ids)) for n, ids in plan.items()}, len(hit), serial


def tables(mutants):
    rows = {}
    for key in ("cls", "op"):
        table = collections.defaultdict(lambda: collections.Counter())
        for m in mutants:
            if m["outcome"] in ("INVALID", "NOT_RUN"):
                table[m[key]]["invalid"] += 1
                continue
            table[m[key]]["valid"] += 1
            table[m[key]][m["outcome"]] += 1
            if m["outcome"] == "CAUGHT" and (m.get("matched") or {}).get("in_class"):
                table[m[key]]["in_class"] += 1
        rows[key] = table
    return rows


def render(mutants):
    out = []
    for key, title in (("cls", "class"), ("op", "operator")):
        out.append(f"{title:34s} valid caught  rate  95% Wilson  in-class nongating waived missed invalid")
        total_v = total_c = total_i = 0
        for name, c in sorted(tables(mutants)[key].items()):
            v, k = c["valid"], c["CAUGHT"]
            lo, hi = wilson(k, v)
            total_v, total_c, total_i = total_v + v, total_c + k, total_i + c["in_class"]
            out.append(f"{name:34s} {v:5d} {k:6d} {100 * k / v if v else 0:5.0f}%  [{100 * lo:3.0f},{100 * hi:3.0f}] {c['in_class']:8d} "
                       f"{c['CAUGHT_NONGATING']:8d} {c['CAUGHT_WAIVED']:6d} {c['MISSED']:6d} {c['invalid']:7d}")
        lo, hi = wilson(total_c, total_v)
        out.append(f"{'all':34s} {total_v:5d} {total_c:6d} {100 * total_c / total_v if total_v else 0:5.0f}%  [{100 * lo:3.0f},{100 * hi:3.0f}] {total_i:8d}")
        out.append("")
    return "\n".join(out)


def build_json(args, binary, results, replay):
    mutants = [m for r in results for m in r["mutants"]]
    commit = None
    pin = HERE.parents[1] / "tests/public_projects/lint_reference.json"
    if pin.exists():
        commit = json.loads(pin.read_text()).get("commit")
    out_mutants = []
    for r in results:
        for m in r["mutants"]:
            rec = {k: m[k] for k in ("ip", "path", "line", "op", "cls", "module", "signals", "outcome", "matched", "run") if k in m}
            rec["operator"] = rec.pop("op")
            rec["class"] = rec.pop("cls")
            if m.get("detail"):
                rec["detail"] = m["detail"]
            out_mutants.append(rec)
    summary = {}
    for key, name in (("cls", "classes"), ("op", "operators")):
        summary[name] = {}
        for k, c in sorted(tables(mutants)[key].items()):
            v, caught = c["valid"], c["CAUGHT"]
            lo, hi = wilson(caught, v)
            summary[name][k] = dict(valid=v, caught=caught, caught_in_class=c["in_class"], nongating=c["CAUGHT_NONGATING"], waived=c["CAUGHT_WAIVED"], missed=c["MISSED"],
                                    invalid=c["invalid"], wilson95=[round(lo, 3), round(hi, 3)])
    summary["invalid_reasons"] = dict(collections.Counter(m["detail"].split(":")[0] for r in results for m in r["mutants"]
                                                          if m["outcome"] == "INVALID"))
    return {"schema": SCHEMA, "tool": "tools/dev/mutation_recall.py", "binary_sha256": hashlib.sha256(pathlib.Path(binary).read_bytes()).hexdigest(),
            "opentitan_commit": commit, "seed": args.seed, "per_op": args.per_op,
            "ips": {r["ip"]: dict(top=r["top"], runs=r["runs"], baseline_gating=r["baseline_open"], candidates=dict(r["candidates"]),
                                  skipped=r["skipped"]) for r in results},
            "summary": summary, "mutants": out_mutants}


def merge_triage(data, triage):
    """Adds the reader's verdict to each mutant keyed `ip|path|line|operator`; `_notes` are kept as notes."""
    counts = collections.Counter()
    for m in data["mutants"]:
        key = f"{m['ip']}|{m['path']}|{m['line']}|{m['operator']}"
        if key in triage:
            m["triage"] = triage[key]
            counts[triage[key]["verdict"]] += 1
    data["notes"] = triage.get("_notes", [])
    data["summary"]["triage"] = dict(counts)
    unread = [m for m in data["mutants"] if m["outcome"] in ("MISSED", "CAUGHT_NONGATING", "CAUGHT_WAIVED") and "triage" not in m]
    if unread:
        raise SystemExit(f"{len(unread)} mutants that are not caught have no triage verdict")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("scratch", type=pathlib.Path)
    parser.add_argument("--replay", type=pathlib.Path, help="the reference_lint.py tree (default: SCRATCH)")
    parser.add_argument("--ip", action="append")
    parser.add_argument("--per-op", type=int, default=3)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--json", type=pathlib.Path)
    parser.add_argument("--memory-gb", type=float, default=3.0, help="cap per SVerity run (0: none)")
    parser.add_argument("--min-available-gb", type=float, default=6.0)
    parser.add_argument("--wait-minutes", type=int, default=30)
    parser.add_argument("--triage", type=pathlib.Path, help="JSON {ip|path|line|operator: {verdict, reason}} merged into --reuse")
    parser.add_argument("--reuse", type=pathlib.Path, help="an earlier result file: merge --triage, print the tables, write --json")
    args = parser.parse_args()
    if args.reuse:
        data = json.loads(args.reuse.read_text())
        if args.triage:
            merge_triage(data, json.loads(args.triage.read_text()))
        mutants = [dict(m, op=m["operator"], cls=m["class"]) for m in data["mutants"]]
        print(render(mutants))
        if args.json:
            args.json.write_text(json.dumps(data, indent=1) + "\n")
        return 0
    replay = (args.replay or args.scratch).resolve()
    results = []
    for ip in args.ip or IPS:
        result = run_ip(args, ip, replay, args.scratch.resolve(), args.binary.resolve())
        results.append(result)
        counts = collections.Counter(m["outcome"] for m in result["mutants"])
        print(json.dumps({"ip": ip, "runs": result["runs"], "outcomes": dict(counts), "skipped": result["skipped"]}), flush=True)
    mutants = [dict(m) for r in results for m in r["mutants"]]
    print(render(mutants))
    if args.json:
        args.json.write_text(json.dumps(build_json(args, args.binary, results, replay), indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
