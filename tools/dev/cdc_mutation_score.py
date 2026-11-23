#!/usr/bin/env python3
"""Small, reproducible CDC mutation tranche over the pinned public CDC corpus.

The runner uses the same manifest, detached checkouts, source lists, dependency
search paths, SDCs, top names, and working directories as tests/public_projects/runner.py.
Only one source file and one site are changed per mutant; all RTL and full reports
stay in the caller's scratch directory.

Optional extensions leave the default invocation and the built-in operators unchanged:
--profile/--deck are passed to sverity, --sdc-dir replaces each manifest SDC with
<design>.sdc, and an operator with "target": "sdc" mutates a scratch copy of the active SDC
("old"/"new" and/or "sdc_append"). An operator may set "accept_unresolved" (with an optional
"require_incomplete_scope") so unresolved findings count as CAUGHT, and
"required_message_contains" to match design-level findings without signals.
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
_MG_SPEC = importlib.util.spec_from_file_location("mem_guard", ROOT / "tools/dev/mem_guard.py")
mem_guard = importlib.util.module_from_spec(_MG_SPEC)
_MG_SPEC.loader.exec_module(mem_guard)
MANIFEST = ROOT / "tests/public_projects/manifest.json"
SCHEMA = "sverity-cdc-mutation-score/1"
PROTOCOL_OBLIGATIONS = {"CDC-024", "CDC-039"}
SEVERITY_RANK = {"INFO": 0, "WARNING": 1, "ERROR": 2, "FATAL": 3}
REQUIRED_OPERATOR_FIELDS = (
    "id", "design", "file", "old", "new", "expected_rule",
    "line_anchor", "defect", "precondition",
)

# Each operator has a narrow syntactic precondition. The expected endpoint is a
# required signal in the matched finding; unrelated or obligation-only reports
# cannot earn a detection.
OPERATORS = [
    dict(id="raw-request-at-payload-capture", design="cc_cdc_2phase", file="cc_cdc_2phase.sv",
         old="req_synced != req_synced_q1", new="async_req_i != req_synced_q1", expected_rule="CDC-028",
         endpoint="cc_cdc_2phase.i_dst.data_dst_q", launch="cc_cdc_2phase.i_src.req_src_q",
         line_anchor="assign dst_data_load", defect="unsynchronized request controls payload capture",
         precondition="The captured payload is loaded on a transition of the synchronized request; async_req_i is the corresponding raw request input."),
    dict(id="raw-ack-at-source-ready", design="cc_cdc_2phase", file="cc_cdc_2phase.sv",
         old="req_src_q == ack_synced", new="req_src_q == async_ack_i", expected_rule="CDC-028",
         endpoint="cc_cdc_2phase.i_src.data_src_q", launch="cc_cdc_2phase.i_dst.ack_dst_q",
         line_anchor="assign ready_o", defect="raw acknowledgement controls source handshake readiness",
         precondition="The source ready comparison consumes the synchronized acknowledgement; async_ack_i is its raw asynchronous input."),
    dict(id="binary-pointer-on-gray-crossing", design="axis_async_fifo", file="axis_async_fifo.v",
         old="wr_ptr_gray_reg <= bin2gray(wr_ptr_temp);", new="wr_ptr_gray_reg <= wr_ptr_temp;", expected_rule="CDC-017",
         endpoint="axis_async_fifo.wr_ptr_gray_sync1_reg", launch="axis_async_fifo.wr_ptr_gray_reg",
         line_anchor="wr_ptr_gray_reg <= bin2gray(wr_ptr_temp);", target_line=426, defect="binary pointer value is driven onto the Gray crossing",
         precondition="The selected assignment is in the FRAME_FIFO branch; the mutation removes only the Gray conversion.",
         equivalent_reason="The edited assignment is inside the FRAME_FIFO branch. The default FRAME_FIFO=0 excludes it from the active design."),
    dict(id="bypass-gray-synchronizer-stage", design="axis_async_fifo", file="axis_async_fifo.v",
         old="rd_ptr_gray_sync2_reg <= rd_ptr_gray_sync1_reg;", new="rd_ptr_gray_sync2_reg <= rd_ptr_gray_reg;", expected_rule="CDC-017",
         endpoint="axis_async_fifo.rd_ptr_gray_sync2_reg", launch="axis_async_fifo.rd_ptr_gray_reg",
         line_anchor="rd_ptr_gray_sync2_reg <= rd_ptr_gray_sync1_reg;", target_line=572, defect="second destination stage samples the asynchronous Gray source directly",
         precondition="The selected second stage is assigned from the first stage in the same destination-clocked process; replacement bypasses exactly one stage."),
]


def replace_one(text, old, new, target_line=None):
    if not old or old == new:
        raise ValueError("mutation must change a nonempty target")
    if target_line is not None:
        lines = text.splitlines(keepends=True)
        if target_line < 1 or target_line > len(lines):
            raise ValueError("precondition failed: target line is out of range")
        count = lines[target_line - 1].count(old)
        if count != 1:
            raise ValueError(f"precondition failed: expected one target at line {target_line}, found {count}")
        lines[target_line - 1] = lines[target_line - 1].replace(old, new, 1)
        return "".join(lines)
    count = text.count(old)
    if count != 1:
        raise ValueError(f"precondition failed: expected one exact target, found {count}")
    return text.replace(old, new, 1)


def checkouts(scratch, manifest):
    repos = {x["name"]: x for x in manifest["repositories"]}
    result = {}
    for spec in manifest["known_good"]:
        names = [spec["repository"], *spec.get("dependencies", [])]
        for name in names:
            if name in result:
                continue
            pin = repos[name]
            path = scratch / "public" / name
            if not path.exists():
                path.parent.mkdir(parents=True, exist_ok=True)
                subprocess.run(["git", "clone", "-q", pin["url"], str(path)], check=True)
            dirty = subprocess.check_output(["git", "-C", str(path), "status", "--porcelain"], text=True).strip()
            if dirty:
                raise RuntimeError(f"{name}: scratch checkout has local changes")
            subprocess.run(["git", "-C", str(path), "checkout", "-q", "--detach", pin["commit"]], check=True)
            actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
            if actual != pin["commit"]:
                raise RuntimeError(f"{name}: expected {pin['commit']}, got {actual}")
            result[name] = path
    return result, repos


def active_sdc(spec, sdc_dir=None):
    """The SDC a design is analysed with: --sdc-dir/<design>.sdc when given, else the manifest SDC."""
    if sdc_dir is None:
        return ROOT / "tests/public_projects" / spec["sdc"]
    path = pathlib.Path(sdc_dir) / (spec["name"] + ".sdc")
    if not path.is_file():
        raise RuntimeError(f"{spec['name']}: missing {spec['name']}.sdc in the SDC directory")
    return path


def sdc_label(path, spec, sdc_dir=None):
    """Repository-relative SDC name that never embeds a local absolute path."""
    if sdc_dir is None:
        return spec["sdc"]
    path = pathlib.Path(path).resolve()
    return str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else path.name


def run(binary, spec, cwd, scratch, dependencies, tag, override=None, profile=None, deck=None, sdc=None):
    scratch.mkdir(parents=True, exist_ok=True)
    report = scratch / (spec["name"] + "-" + tag + ".json")
    # Preserve the manifest's original arguments; only substitute dependency path
    # placeholders and the explicitly mutated primary source.
    args = ["--goal", "cdc"]
    if profile:
        args += ["--profile", profile]
    if deck:
        args += ["--deck", deck]
    args += ["--top", spec["name"], "--root", ".", "--sdc", str(sdc or ROOT / "tests/public_projects" / spec["sdc"])]
    args += [x.format(**dependencies) for x in spec.get("args", [])]
    args += [str(override[1]) if override and x == override[0] else x for x in spec["sources"]]
    report.unlink(missing_ok=True)
    cmd, unit = mem_guard.build_command(
        [str(binary), "--no-color", "--json", str(report), *args],
        profile="public", label=f"{spec['name']}-{tag}", exec_mode=True,
    )
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, timeout=300)
    except subprocess.TimeoutExpired as exc:
        mem_guard.stop_unit(unit)
        raise RuntimeError(f"{spec['name']}: TIMEOUT (300s)") from exc
    oom, detail = mem_guard.inspect_oom(unit, p.stderr, p.returncode,
                                        profile="public", label=f"{spec['name']}-{tag}")
    if oom:
        raise RuntimeError(f"{spec['name']}: MEMORY LIMIT EXCEEDED: {detail}")
    if not report.exists():
        raise RuntimeError(f"{spec['name']}: no report (exit {p.returncode}): {p.stderr[-500:]}")
    return json.loads(report.read_text()), p.returncode, hashlib.sha256(report.read_bytes()).hexdigest()


def signal_match(candidate, expected):
    return candidate == expected or candidate.endswith("." + expected)


def endpoint_match(d, endpoint):
    signals = list(d.get("signals") or [])
    crossing = d.get("crossing") or {}
    for role in ("launch", "capture"):
        signal = (crossing.get(role) or {}).get("signal")
        if signal:
            signals.append(signal)
    return any(signal_match(s, endpoint) for s in signals)


def signals_match(d, required_signals):
    signals = [s for s in (d.get("signals") or []) if isinstance(s, str) and s]
    return bool(signals) and all(any(signal_match(s, req) for s in signals) for req in required_signals)


def source_match(d, required_source):
    crossing = d.get("crossing") or {}
    launch_signal = (crossing.get("launch") or {}).get("signal")
    if launch_signal:
        return signal_match(launch_signal, required_source)
    if crossing:
        return False
    return signals_match(d, [required_source])


def _require_nonempty_str(op, field):
    value = op.get(field)
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"operator missing non-empty string field '{field}'")
    return value


def validate_match_spec(op):
    """Return the matching mode: "crossing", "source_only" or "message_only"."""
    if not isinstance(op, dict):
        raise ValueError("operator must be a mapping")
    _require_nonempty_str(op, "expected_rule")
    has_endpoint = op.get("endpoint") is not None
    has_launch = op.get("launch") is not None
    has_req_signals = op.get("required_signals") is not None
    has_req_source = op.get("required_source") is not None
    has_message = op.get("required_message_contains") is not None
    if has_message:
        _require_nonempty_str(op, "required_message_contains")
    if has_launch and not has_endpoint:
        raise ValueError("operator 'launch' requires 'endpoint'")
    if (has_endpoint or has_launch) and (has_req_signals or has_req_source):
        raise ValueError("operator cannot mix endpoint/launch with required_signals/required_source")
    if has_endpoint:
        _require_nonempty_str(op, "endpoint")
        if has_launch:
            _require_nonempty_str(op, "launch")
        return "crossing"
    if not (has_req_signals or has_req_source):
        if has_message:
            # Design-level rules (for example CST-009) carry no signals to match.
            return "message_only"
        raise ValueError("operator must specify 'endpoint', source-only matching ('required_signals' / "
                         "'required_source') or 'required_message_contains'")
    if has_req_signals:
        req_signals = op["required_signals"]
        if not isinstance(req_signals, (list, tuple)) or not req_signals:
            raise ValueError("operator 'required_signals' must be a non-empty list of strings")
        if not all(isinstance(s, str) and s.strip() for s in req_signals):
            raise ValueError("operator 'required_signals' entries must be non-empty strings")
    if has_req_source:
        _require_nonempty_str(op, "required_source")
    return "source_only"


def operator_target(op):
    target = op.get("target", "rtl")
    if target not in ("rtl", "sdc"):
        raise ValueError(f"{op.get('id')}: target must be 'rtl' or 'sdc'")
    return target


def validate_operator(op):
    if not isinstance(op, dict):
        raise ValueError("operator must be a mapping")
    target = operator_target(op)
    if target == "rtl":
        for field in REQUIRED_OPERATOR_FIELDS:
            _require_nonempty_str(op, field)
        if op.get("sdc_append") is not None:
            raise ValueError(f"{op['id']}: sdc_append is only valid for target 'sdc'")
    else:
        for field in ("id", "design", "file", "expected_rule", "defect", "precondition"):
            _require_nonempty_str(op, field)
        has_old, has_new = op.get("old") is not None, op.get("new") is not None
        if has_old != has_new:
            raise ValueError(f"{op['id']}: sdc operator needs both 'old' and 'new' or neither")
        if has_old:
            _require_nonempty_str(op, "old")
            _require_nonempty_str(op, "new")
            _require_nonempty_str(op, "line_anchor")
        if op.get("sdc_append") is not None:
            _require_nonempty_str(op, "sdc_append")
        elif not has_old:
            raise ValueError(f"{op['id']}: sdc operator needs 'old'/'new' or 'sdc_append'")
    if op.get("old") is not None and op["old"] == op.get("new"):
        raise ValueError(f"{op['id']}: mutation must change target text")
    target_line = op.get("target_line")
    if target_line is not None and (not isinstance(target_line, int) or isinstance(target_line, bool) or target_line < 1):
        raise ValueError(f"{op['id']}: target_line must be a positive integer")
    if op.get("equivalent_reason") is not None:
        _require_nonempty_str(op, "equivalent_reason")
    if op.get("original_sha256") is not None:
        digest = _require_nonempty_str(op, "original_sha256")
        if len(digest) != 64 or any(c not in "0123456789abcdefABCDEF" for c in digest):
            raise ValueError(f"{op['id']}: original_sha256 must be a 64-character hex digest")
    if op.get("accept_unresolved") is not None and not isinstance(op["accept_unresolved"], bool):
        raise ValueError(f"{op['id']}: accept_unresolved must be a boolean")
    if op.get("require_incomplete_scope") is not None:
        _require_nonempty_str(op, "require_incomplete_scope")
        if op.get("accept_unresolved") is not True:
            raise ValueError(f"{op['id']}: require_incomplete_scope needs accept_unresolved true")
    validate_match_spec(op)
    return op


def validate_operators(ops):
    if not isinstance(ops, (list, tuple)) or not ops:
        raise ValueError("operators must be a non-empty list")
    seen = set()
    validated = []
    for op in ops:
        validate_operator(op)
        if op["id"] in seen:
            raise ValueError(f"duplicate operator id: {op['id']}")
        seen.add(op["id"])
        validated.append(op)
    return validated


def load_operators(path):
    data = json.loads(pathlib.Path(path).read_text())
    if isinstance(data, dict):
        if "operators" in data:
            ops = data["operators"]
        elif "candidates" in data:
            ops = data["candidates"]
        else:
            raise ValueError(f"{path}: operator JSON object must contain 'operators' or 'candidates'")
    else:
        ops = data
    return validate_operators(ops)


def finding_matches(d, op):
    mode = validate_match_spec(op)
    if d.get("rule_id") != op["expected_rule"]:
        return False
    if op.get("required_message_contains") is not None and op["required_message_contains"] not in d.get("message", ""):
        return False
    if mode == "crossing":
        if not endpoint_match(d, op["endpoint"]):
            return False
        if op.get("launch") is not None and not endpoint_match(d, op["launch"]):
            return False
        return True
    if mode == "message_only":
        return True
    if op.get("required_signals") is not None and not signals_match(d, op["required_signals"]):
        return False
    if op.get("required_source") is not None and not source_match(d, op["required_source"]):
        return False
    return True


def classify(d):
    if d.get("disposition") == "WAIVED":
        return "waived"
    if d.get("disposition") == "NOT_ANALYSED":
        return "not_analysed"
    if d.get("disposition") == "RESOLVED":
        return "resolved"
    if d.get("disposition") == "OBLIGATION" or d.get("rule_id") in PROTOCOL_OBLIGATIONS:
        return "generic_obligation"
    confidence = d.get("confidence", "unresolved").lower()
    if confidence == "established":
        return "established"
    if confidence == "heuristic":
        return "heuristic"
    if confidence in ("unresolved", "not_analysed", "not-analysed"):
        return "unresolved"
    return "informational"


def scope_incomplete(report, scope):
    """True when the report lists `scope` with INCOMPLETE coverage (key: analysis_coverage, or legacy coverage)."""
    scopes = (report.get("coverage") or {}).get("scopes") or []
    return any(s.get("scope") == scope and (s.get("analysis_coverage") or s.get("coverage")) == "INCOMPLETE"
               for s in scopes if isinstance(s, dict))


def score(base, mutant, op):
    mode = validate_match_spec(op)
    if mutant.get("summary", {}).get("execution_status") != "SUCCESS":
        rejection = any(d.get("origin") == "slang" and d.get("severity") in ("ERROR", "FATAL")
                        for d in mutant.get("diagnostics", []))
        return ("INVALID" if rejection else "EXECUTION_ERROR"), None
    base_max_sev = {}
    for d in base.get("diagnostics", []):
        fp = d.get("fingerprint")
        rank = SEVERITY_RANK.get(d.get("severity"), -1)
        if fp not in base_max_sev or rank > base_max_sev[fp]:
            base_max_sev[fp] = rank
    fresh = [
        d for d in mutant.get("diagnostics", [])
        if d.get("fingerprint") not in base_max_sev
        or SEVERITY_RANK.get(d.get("severity"), -1) > base_max_sev[d.get("fingerprint")]
    ]
    candidates = [d for d in fresh if finding_matches(d, op)]
    if candidates:
        # Unresolved findings gate only when the operator explicitly accepts them (fail-closed default),
        # and then only if the required analysis scope is reported INCOMPLETE in the mutant run.
        accepted = ("established", "heuristic")
        if op.get("accept_unresolved") is True and (
                op.get("require_incomplete_scope") is None or scope_incomplete(mutant, op["require_incomplete_scope"])):
            accepted += ("unresolved",)

        def gates(diagnostic):
            return classify(diagnostic) in accepted and diagnostic.get("severity") in ("ERROR", "FATAL", "WARNING")

        d = next((finding for finding in candidates if gates(finding)), candidates[0])
        category = classify(d)
        outcome = "CAUGHT" if gates(d) else "CAUGHT_NONGATING"
        matched = dict(rule=d.get("rule_id"), confidence=d.get("confidence"), severity=d.get("severity"),
                       disposition=d.get("disposition"), line=(d.get("location") or {}).get("line"),
                       classification=category)
        if op.get("accept_unresolved") is True:
            matched["accept_unresolved"] = True
            if op.get("require_incomplete_scope") is not None:
                matched["require_incomplete_scope"] = op["require_incomplete_scope"]
        if op.get("required_message_contains") is not None:
            matched["required_message_contains"] = op["required_message_contains"]
        if mode == "crossing":
            matched["endpoint"] = op["endpoint"]
        else:
            matched["signals"] = list(d.get("signals") or [])
            if op.get("required_source") is not None:
                matched["required_source"] = op["required_source"]
            if op.get("required_signals") is not None:
                matched["required_signals"] = list(op["required_signals"])
        return ("EQUIVALENT" if op.get("equivalent_reason") else outcome), matched
    return ("EQUIVALENT" if op.get("equivalent_reason") else "MISSED"), None


def removed_findings(base, mutant):
    remaining = {d["fingerprint"] for d in mutant["diagnostics"]}
    return [{key: d[key] for key in ("fingerprint", "rule_id", "severity", "confidence", "signals") if key in d}
            for d in base["diagnostics"] if d["fingerprint"] not in remaining]


def inventory(reports):
    rows = collections.Counter()
    for design, report in reports.items():
        for d in report.get("diagnostics", []):
            location = d.get("location") or {}
            evidence = d.get("evidence") or []
            obligation = d.get("obligation_id") or next((e.get("text", "") for e in evidence if e.get("kind") == "proof_obligation"), "")
            site = ":".join(str(x) for x in (location.get("file"), location.get("line")) if x is not None) or ", ".join(d.get("signals", [])) or "design"
            rows[(design, d.get("rule_id", "unknown"), site, obligation, classify(d),
                  d.get("severity", "unknown"), d.get("message", ""))] += int(d.get("count", 1))
    return [dict(design=k[0], rule=k[1], site=k[2], obligation=k[3], classification=k[4], severity=k[5], message=k[6], count=n)
            for k, n in sorted(rows.items())]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binary", type=pathlib.Path)
    ap.add_argument("scratch", type=pathlib.Path)
    ap.add_argument("--json", type=pathlib.Path, required=True)
    ap.add_argument("--operators", type=pathlib.Path, default=None,
                    help="Optional JSON operator/candidate list (defaults to the frozen wave-1 OPERATORS).")
    ap.add_argument("--profile", default=None, help="Optional sverity --profile (for example asic); default: not passed.")
    ap.add_argument("--deck", default=None, help="Optional sverity --deck (for example cdc); default: not passed.")
    ap.add_argument("--sdc-dir", type=pathlib.Path, default=None,
                    help="Directory of <design>.sdc files that replace each manifest SDC (for example the reviewed SDCs).")
    opt = ap.parse_args()
    if opt.json.exists():
        ap.error("output already exists; choose a new JSON path")
    operators = load_operators(opt.operators) if opt.operators else validate_operators(OPERATORS)
    scratch = opt.scratch.resolve()
    if scratch.is_relative_to(ROOT):
        ap.error("scratch must be outside the repository")
    scratch.mkdir(parents=True, exist_ok=True)
    sdc_dir = opt.sdc_dir.resolve() if opt.sdc_dir else None
    manifest = json.loads(MANIFEST.read_text())
    specs = {x["name"]: x for x in manifest["known_good"]}
    checkouts_by_name, pins = checkouts(scratch, manifest)
    dependencies = {name: str(path) for name, path in checkouts_by_name.items()}
    # Include all five baselines in the residual inventory, including designs
    # without mutants in this deliberately small tranche.
    base_names = [x["name"] for x in manifest["known_good"]]
    baselines = {}
    metadata = {}
    for name in base_names:
        spec = specs[name]
        repo = checkouts_by_name[spec["repository"]]
        cwd = repo / spec["workdir"]
        report, rc, digest = run(opt.binary.resolve(), spec, cwd, scratch / "reports", dependencies, "baseline",
                                 profile=opt.profile, deck=opt.deck, sdc=active_sdc(spec, sdc_dir))
        if report.get("summary", {}).get("execution_status") != "SUCCESS" or rc < 0 or rc & (8 | 64):
            raise RuntimeError(f"{name}: baseline execution failed (exit {rc})")
        baselines[name] = report
        sdc_path = active_sdc(spec, sdc_dir)
        config_identity = dict(goal="cdc", top=spec["name"], root=".", sources=spec["sources"],
                              manifest_args=spec.get("args", []), sdc=sdc_label(sdc_path, spec, sdc_dir),
                              sdc_sha256=hashlib.sha256(sdc_path.read_bytes()).hexdigest())
        # Added only when passed, so the default invocation identity is unchanged.
        if opt.profile:
            config_identity["profile"] = opt.profile
        if opt.deck:
            config_identity["deck"] = opt.deck
        metadata[name] = dict(repository=spec["repository"], repository_url=pins[spec["repository"]]["url"],
                              commit=pins[spec["repository"]]["commit"], top=name,
                              invocation=config_identity,
                              config_sha256=hashlib.sha256(json.dumps(config_identity, sort_keys=True).encode()).hexdigest(),
                              report_sha256=digest, execution=report["summary"].get("execution_status"),
                              analysis_coverage=report["summary"].get("analysis_coverage"),
                              review_status=report["summary"].get("review_status"),
                              diagnostic_count=len(report.get("diagnostics", [])))
    mutants = []
    for idx, op in enumerate(operators, 1):
        spec = specs[op["design"]]
        target = operator_target(op)
        checkout = checkouts_by_name[spec["repository"]]
        cwd = checkout / spec["workdir"]
        sdc_base = active_sdc(spec, sdc_dir)
        label = sdc_label(sdc_base, spec, sdc_dir)
        if target == "sdc":
            if op["file"] not in ("active.sdc", sdc_base.name, spec["name"] + ".sdc", spec["sdc"]):
                raise RuntimeError(f"{op['id']}: file must name the active SDC ('active.sdc' or '{sdc_base.name}')")
            original = sdc_base
        else:
            if spec["sources"].count(op["file"]) != 1:
                raise RuntimeError(f"{op['id']}: source must appear exactly once in the manifest")
            original = cwd / op["file"]
        text = original.read_text()
        orig_sha256 = hashlib.sha256(text.encode()).hexdigest()
        if op.get("original_sha256") and orig_sha256 != op["original_sha256"]:
            raise RuntimeError(f"{op['id']}: original_sha256 mismatch (expected {op['original_sha256']}, got {orig_sha256})")
        if target == "rtl" and op.get("equivalent_reason") and "FRAME_FIFO" in op["equivalent_reason"] and (
                spec.get("args") or "parameter FRAME_FIFO = 0," not in text):
            raise RuntimeError(f"{op['id']}: default FRAME_FIFO equivalence precondition failed")
        if op.get("line_anchor") is not None and op["line_anchor"] not in text:
            raise RuntimeError(f"{op['id']}: line anchor precondition failed")
        changed = text
        if op.get("old") is not None:
            changed = replace_one(changed, op["old"], op["new"], op.get("target_line"))
        if op.get("sdc_append") is not None:
            changed += ("" if changed.endswith("\n") else "\n") + op["sdc_append"] + ("" if op["sdc_append"].endswith("\n") else "\n")
        if changed == text:
            raise RuntimeError(f"{op['id']}: mutation did not change the target")
        dest = scratch / "mutants" / f"{idx:02d}-{op['id']}" / (("active.sdc") if target == "sdc" else op["file"])
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)
        tag = f"{idx:02d}-{op['id']}"
        # The paired baseline and the mutant read the same scratch path, so path-derived
        # evidence and fingerprints match; only the file contents change between the runs.
        override = None if target == "sdc" else (op["file"], dest)
        sdc_run = dest if target == "sdc" else sdc_base
        common = dict(profile=opt.profile, deck=opt.deck, sdc=sdc_run)
        paired, paired_rc, paired_digest = run(opt.binary.resolve(), spec, cwd, scratch / "reports",
                                               dependencies, tag + "-baseline", override, **common)
        if paired.get("summary", {}).get("execution_status") != "SUCCESS" or paired_rc < 0 or paired_rc & (8 | 64):
            raise RuntimeError(f"{op['id']}: paired baseline failed (exit {paired_rc})")
        dest.write_text(changed)
        report, rc, digest = run(opt.binary.resolve(), spec, cwd, scratch / "reports", dependencies, tag, override, **common)
        if rc < 0 or rc & 64 or (rc & 8 and report.get("summary", {}).get("execution_status") == "SUCCESS"):
            raise RuntimeError(f"{op['id']}: mutation run failed (exit {rc})")
        outcome, match = score(paired, report, op)
        if op.get("target_line"):
            line = op["target_line"]
        elif op.get("old") is not None:
            line = next((i for i, row in enumerate(text.splitlines(), 1) if op["old"] in row), None)
        else:
            line = len(text.splitlines()) + 1
        entry = dict(id=op["id"], design=op["design"],
                     source_path=label if target == "sdc" else f"{spec['workdir']}/{op['file']}",
                     source_commit=pins[spec["repository"]]["commit"], original_sha256=orig_sha256,
                     mutant_sha256=hashlib.sha256(changed.encode()).hexdigest(), report_sha256=digest,
                     paired_baseline_sha256=paired_digest, line=line,
                     operator=op["id"], precondition=op["precondition"], defect=op["defect"], expected_rule=op["expected_rule"],
                     outcome=outcome, matched=match, exit_code=rc,
                     disappeared=removed_findings(paired, report), triage="independently_reviewed",
                     equivalence_assessment=op.get("equivalent_reason", "changes active logic in the pinned configuration"))
        if target == "sdc":
            entry.update(target="sdc", original_sdc_sha256=orig_sha256,
                         mutant_sdc_sha256=entry["mutant_sha256"])
        for key in ("endpoint", "launch", "required_source", "required_message_contains", "require_incomplete_scope"):
            if op.get(key) is not None:
                entry[key] = op[key]
        if op.get("required_signals") is not None:
            entry["required_signals"] = list(op["required_signals"])
        if op.get("accept_unresolved") is True:
            entry["accept_unresolved"] = True
        mutants.append(entry)
    git_head = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"], text=True).strip())
    out = dict(schema=SCHEMA, tool="tools/dev/cdc_mutation_score.py", tool_commit=git_head,
               tool_git_dirty=dirty,
               tool_sha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
               binary_sha256=hashlib.sha256(opt.binary.resolve().read_bytes()).hexdigest(),
               manifest_sha256=hashlib.sha256(MANIFEST.read_bytes()).hexdigest(),
               repository_pins={name: dict(url=pin["url"], commit=pin["commit"])
                                for name, pin in sorted(pins.items())}, designs=metadata,
               sample=dict(operators=len(mutants), mutants_by_design=dict(collections.Counter(x["design"] for x in mutants)),
                           covered_operator_families=sorted({x["operator"] for x in mutants}),
                           absent_designs=sorted(set(base_names) - {x["design"] for x in mutants}),
                           note="Targeted structural probes; not a representative or release qualification sample."),
               counts=dict(collections.Counter(x["outcome"] for x in mutants)), mutants=mutants,
               mutant_triage=dict(independently_reviewed=len(mutants), pending=0,
                                  invalid=sum(x["outcome"] == "INVALID" for x in mutants),
                                  equivalent=sum(x["outcome"] == "EQUIVALENT" for x in mutants)),
               residual_inventory=inventory(baselines))
    if opt.profile or opt.deck or sdc_dir:
        out["options"] = {k: v for k, v in dict(
            profile=opt.profile, deck=opt.deck,
            sdc_dir=(str(sdc_dir.relative_to(ROOT)) if sdc_dir.is_relative_to(ROOT) else sdc_dir.name) if sdc_dir else None,
        ).items() if v}
    residual_counts = collections.Counter()
    for row in out["residual_inventory"]:
        residual_counts[row["classification"]] += row["count"]
    out["residual_counts"] = {key: residual_counts.get(key, 0) for key in
                               ("established", "heuristic", "unresolved", "generic_obligation", "waived",
                                "resolved", "not_analysed", "informational")}
    opt.json.parent.mkdir(parents=True, exist_ok=True)
    with opt.json.open("x") as output:
        output.write(json.dumps(out, indent=2, sort_keys=True) + "\n")
    print(json.dumps(out["counts"], sort_keys=True))
    return int(out["counts"].get("EXECUTION_ERROR", 0) != 0)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.SubprocessError, RuntimeError, KeyError, ValueError) as exc:
        print(f"cdc mutation score: {exc}", file=sys.stderr)
        sys.exit(1)
