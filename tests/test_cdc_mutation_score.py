"""Unit checks for the CDC mutation runner's fail-closed scoring primitives."""
import contextlib
import importlib.util
import io
import json
import os
import pathlib
import stat
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("cdc_mutation_score", ROOT / "tools/dev/cdc_mutation_score.py")
score = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(score)


def _valid_crossing_op(**overrides):
    op = {
        "id": "sample-crossing-mutant",
        "design": "cc_cdc_4phase",
        "file": "cc_cdc_4phase.sv",
        "target_line": 336,
        "line_anchor": "assign async_ack_o = ack_dst_q;",
        "old": "assign async_ack_o = ack_dst_q;",
        "new": "assign async_ack_o = ack_dst_d;",
        "defect": "combinational next-state ack drives asynchronous handshake output",
        "precondition": "async_ack_o is driven by ack_dst_q; ack_dst_d is combinational.",
        "expected_rule": "CDC-008",
        "launch": "cc_cdc_4phase.i_dst.state_q",
        "endpoint": "cc_cdc_4phase.i_src.i_sync.reg_q",
    }
    op.update(overrides)
    return op


def _valid_source_only_op(**overrides):
    op = {
        "id": "gray-fifo-one-extra-read-pointer-stage",
        "design": "cc_cdc_fifo_gray",
        "file": "cc_cdc_fifo_gray.sv",
        "target_line": 330,
        "line_anchor": "tc_sync #(.Stages(SyncStages)) i_sync (",
        "old": "tc_sync #(.Stages(SyncStages))",
        "new": "tc_sync #(.Stages(SyncStages + (i == 0)))",
        "defect": "lane 0 has one extra synchronizer stage causing multi-bit depth skew",
        "precondition": "The generate loop instantiates per-bit tc_sync lanes.",
        "expected_rule": "CDC-032",
        "required_signals": ["cc_cdc_fifo_gray.i_dst.i_dst_ptr.rptr_q"],
    }
    op.update(overrides)
    return op


class MutationScoreTests(unittest.TestCase):
    def test_exact_precondition_is_required(self):
        with self.assertRaises(ValueError):
            score.replace_one("a <= b;\na <= b;\n", "a <= b", "a <= c")

    def test_line_bounded_edit_changes_only_one_target(self):
        original = "first <= src;\nsecond <= first;\n"
        changed = score.replace_one(original, "second <= first;", "second <= src;", 2)
        self.assertEqual(changed, "first <= src;\nsecond <= src;\n")

    def test_detection_needs_new_rule_and_endpoint_match(self):
        baseline = {"diagnostics": []}
        finding = {"fingerprint": "new", "rule_id": "CDC-008", "severity": "WARNING",
                   "confidence": "established", "signals": ["top.capture_q"]}
        mutant = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": [finding]}
        op = {"expected_rule": "CDC-008", "endpoint": "capture_q"}
        self.assertEqual(score.score(baseline, mutant, op)[0], "CAUGHT")
        op["endpoint"] = "other_q"
        self.assertEqual(score.score(baseline, mutant, op)[0], "MISSED")

    def test_unresolved_match_is_nongating(self):
        finding = {"fingerprint": "new", "rule_id": "CDC-008", "severity": "WARNING",
                   "confidence": "unresolved", "signals": ["top.capture_q"]}
        mutant = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": [finding]}
        self.assertEqual(score.score({"diagnostics": []}, mutant,
                                     {"expected_rule": "CDC-008", "endpoint": "capture_q"})[0], "CAUGHT_NONGATING")

    def test_baseline_match_and_substrings_do_not_count(self):
        finding = dict(fingerprint="same", rule_id="CDC-028", severity="ERROR", confidence="established",
                       signals=["top.raw", "top.capture_q"])
        baseline = {"diagnostics": [finding]}
        mutant = {"summary": {"execution_status": "SUCCESS"},
                  "diagnostics": [finding, dict(finding, fingerprint="other", signals=["top.raw", "top.capture_q_extra"])]}
        op = {"expected_rule": "CDC-028", "endpoint": "capture_q", "launch": "raw"}
        self.assertEqual(score.score(baseline, mutant, op)[0], "MISSED")
        mutant["diagnostics"][1] = dict(finding, fingerprint="different_launch", signals=["top.not_raw", "top.capture_q"])
        self.assertEqual(score.score(baseline, mutant, op)[0], "MISSED")

    def test_errors_and_generic_obligations_cannot_count_as_detections(self):
        finding = dict(fingerprint="new", rule_id="CDC-024", severity="WARNING", confidence="heuristic",
                       disposition="REVIEW_REQUIRED", signals=["top.capture_q"])
        op = {"expected_rule": "CDC-024", "endpoint": "capture_q"}
        baseline = {"diagnostics": []}
        mutant = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": [finding]}
        self.assertEqual(score.score(baseline, mutant, op)[0], "CAUGHT_NONGATING")
        mutant["summary"]["execution_status"] = "ERROR"
        self.assertEqual(score.score(baseline, mutant, op)[0], "EXECUTION_ERROR")
        mutant["diagnostics"].append({"origin": "slang", "severity": "ERROR"})
        self.assertEqual(score.score(baseline, mutant, op)[0], "INVALID")

    def test_equivalent_mutant_earns_no_credit_and_retains_its_warning(self):
        finding = dict(fingerprint="new", rule_id="CDC-017", severity="ERROR", confidence="heuristic",
                       signals=["top.capture_q"])
        baseline = {"diagnostics": [dict(finding, fingerprint="removed")]}
        mutant = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": [finding]}
        op = {"expected_rule": "CDC-017", "endpoint": "capture_q", "equivalent_reason": "dead default branch"}
        outcome, evidence = score.score(baseline, mutant, op)
        self.assertEqual(outcome, "EQUIVALENT")
        self.assertEqual(evidence["severity"], "ERROR")
        self.assertEqual(score.removed_findings(baseline, mutant)[0]["fingerprint"], "removed")

    def test_cdc032_source_signal_only_match_and_rejections(self):
        baseline = {"diagnostics": []}
        src = "cc_cdc_fifo_gray.i_dst.i_dst_ptr.rptr_q"
        cdc032 = {
            "fingerprint": "cdc032-rptr",
            "rule_id": "CDC-032",
            "severity": "ERROR",
            "confidence": "established",
            "disposition": "OPEN",
            "signals": [src],
        }
        mutant = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": [cdc032]}

        op_signals = {"expected_rule": "CDC-032", "required_signals": [src]}
        outcome, matched = score.score(baseline, mutant, op_signals)
        self.assertEqual(outcome, "CAUGHT")
        self.assertEqual(matched["rule"], "CDC-032")
        self.assertEqual(matched["signals"], [src])
        self.assertEqual(matched["required_signals"], [src])
        self.assertNotIn("endpoint", matched)

        op_source = {"expected_rule": "CDC-032", "required_source": src}
        outcome_src, matched_src = score.score(baseline, mutant, op_source)
        self.assertEqual(outcome_src, "CAUGHT")
        self.assertEqual(matched_src["required_source"], src)

        # Wrong signal, substring trap, missing signal, and wrong rule must all MISS.
        for bad_diag in (
            dict(cdc032, fingerprint="wrong-sig", signals=["cc_cdc_fifo_gray.i_src.i_src_ptr.wptr_q"]),
            dict(cdc032, fingerprint="substr-sig", signals=[src + "_extra"]),
            dict(cdc032, fingerprint="empty-sig", signals=[]),
            dict(cdc032, fingerprint="wrong-rule", rule_id="CDC-017"),
        ):
            bad_mutant = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": [bad_diag]}
            self.assertEqual(score.score(baseline, bad_mutant, op_signals)[0], "MISSED")
            self.assertEqual(score.score(baseline, bad_mutant, op_source)[0], "MISSED")

        # Unresolved CDC-032 is non-gating, and baseline CDC-032 cannot earn credit.
        unresolved = {"summary": {"execution_status": "SUCCESS"},
                      "diagnostics": [dict(cdc032, fingerprint="u1", confidence="unresolved")]}
        self.assertEqual(score.score(baseline, unresolved, op_signals)[0], "CAUGHT_NONGATING")
        self.assertEqual(score.score({"diagnostics": [cdc032]}, mutant, op_signals)[0], "MISSED")

    def test_required_source_does_not_match_capture_endpoint(self):
        finding = {
            "fingerprint": "cross-1",
            "rule_id": "CDC-032",
            "severity": "ERROR",
            "confidence": "established",
            "signals": ["top.src_bus", "top.dst_bus"],
            "crossing": {
                "launch": {"signal": "top.src_bus"},
                "capture": {"signal": "top.dst_bus"},
            },
        }
        mutant = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": [finding]}
        self.assertEqual(score.score({"diagnostics": []}, mutant,
                                     {"expected_rule": "CDC-032", "required_source": "top.src_bus"})[0], "CAUGHT")
        self.assertEqual(score.score({"diagnostics": []}, mutant,
                                     {"expected_rule": "CDC-032", "required_source": "top.dst_bus"})[0], "MISSED")

    def test_crossing_mode_still_requires_endpoint_and_rejects_omitted_target(self):
        finding = {"fingerprint": "new", "rule_id": "CDC-028", "severity": "ERROR",
                   "confidence": "established", "signals": ["top.src"]}
        mutant = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": [finding]}
        with self.assertRaises(ValueError):
            score.score({"diagnostics": []}, mutant, {"expected_rule": "CDC-028"})
        self.assertEqual(score.score({"diagnostics": []}, mutant,
                                     {"expected_rule": "CDC-028", "launch": "top.src", "endpoint": "top.dst"})[0], "MISSED")

    def test_operator_schema_validation(self):
        self.assertEqual(len(score.validate_operators(score.OPERATORS)), len(score.OPERATORS))
        score.validate_operator(_valid_crossing_op())
        score.validate_operator(_valid_source_only_op())

        for required in ("id", "design", "file", "old", "new", "expected_rule", "line_anchor", "defect", "precondition"):
            bad = _valid_crossing_op()
            del bad[required]
            with self.assertRaises(ValueError):
                score.validate_operator(bad)

        # Cannot mix crossing endpoint/launch with source-only matchers.
        with self.assertRaises(ValueError):
            score.validate_operator(_valid_crossing_op(required_signals=["cc_cdc_4phase.i_dst.state_q"]))
        with self.assertRaises(ValueError):
            score.validate_operator(_valid_source_only_op(endpoint="cc_cdc_fifo_gray.i_src.i_src_ptr.gen_sync[0].i_sync.reg_q"))

        # Must specify at least one matching target.
        no_target = _valid_crossing_op()
        del no_target["endpoint"]
        del no_target["launch"]
        with self.assertRaises(ValueError):
            score.validate_operator(no_target)

        # Invalid target_line, unchanged mutation, empty required_signals, duplicate ids.
        with self.assertRaises(ValueError):
            score.validate_operator(_valid_crossing_op(target_line=0))
        with self.assertRaises(ValueError):
            score.validate_operator(_valid_crossing_op(target_line=True))
        with self.assertRaises(ValueError):
            score.validate_operator(_valid_crossing_op(new="assign async_ack_o = ack_dst_q;"))
        with self.assertRaises(ValueError):
            score.validate_operator(_valid_source_only_op(required_signals=[]))
        with self.assertRaises(ValueError):
            score.validate_operators([_valid_crossing_op(), _valid_crossing_op()])

        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "candidates.json"
            path.write_text(json.dumps({"candidates": [_valid_crossing_op(), _valid_source_only_op()]}))
            loaded = score.load_operators(path)
            self.assertEqual(len(loaded), 2)


def _sdc_op(**overrides):
    op = {
        "id": "drop-async-clock-groups",
        "target": "sdc",
        "design": "cc_cdc_2phase",
        "file": "active.sdc",
        "line_anchor": "set_clock_groups -asynchronous",
        "old": "set_clock_groups -asynchronous",
        "new": "set_clock_groups -logically_exclusive",
        "defect": "asynchronous clock relationship removed from the constraints",
        "precondition": "The active SDC declares exactly one asynchronous clock group.",
        "expected_rule": "CDC-003",
        "required_signals": ["cc_cdc_2phase.i_dst.data_dst_q"],
    }
    op.update(overrides)
    return op


def _ok(diagnostics, scopes=None):
    report = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": diagnostics}
    if scopes is not None:
        report["coverage"] = {"scopes": scopes}
    return report


def _finding(**overrides):
    d = {"fingerprint": "f1", "rule_id": "CST-009", "severity": "WARNING", "confidence": "established",
         "message": "unsupported SDC command 'set_clock_latency' is ignored", "signals": []}
    d.update(overrides)
    return d


class ExtendedValidationTests(unittest.TestCase):
    def test_sdc_target_validation(self):
        score.validate_operator(_sdc_op())
        score.validate_operator(_sdc_op(old=None, new=None, line_anchor=None,
                                        sdc_append="set_false_path -from [get_clocks src_clk_i]"))
        score.validate_operator(_sdc_op(file="cc_cdc_2phase.sdc"))
        score.validate_operators([_sdc_op(), _valid_crossing_op()])
        for bad in (
            {"old": None, "new": None},                        # neither replacement nor append
            {"new": None},                                     # old without new
            {"old": None},                                     # new without old
            {"new": _sdc_op()["old"]},                         # no-op replacement
            {"target": "netlist"},                             # unknown target
            {"file": ""},
            {"sdc_append": ""},
            {"line_anchor": None},                             # replacement needs an anchor
        ):
            op = _sdc_op()
            for key, value in bad.items():
                if value is None:
                    op.pop(key, None)
                else:
                    op[key] = value
            with self.assertRaises(ValueError, msg=str(bad)):
                score.validate_operator(op)
        with self.assertRaises(ValueError):
            score.validate_operator(_valid_crossing_op(sdc_append="set_false_path"))
        with self.assertRaises(ValueError):
            score.validate_operator(_valid_crossing_op(target="rtl", sdc_append="set_false_path"))
        score.validate_operator(_valid_crossing_op(target="rtl"))

    def test_message_only_and_combined_specs(self):
        design_level = {"expected_rule": "CST-009", "required_message_contains": "set_clock_latency"}
        self.assertEqual(score.validate_match_spec(design_level), "message_only")
        self.assertEqual(score.validate_match_spec(dict(design_level, endpoint="top.q")), "crossing")
        self.assertEqual(score.validate_match_spec(dict(design_level, required_signals=["top.q"])), "source_only")
        self.assertEqual(score.validate_match_spec(dict(design_level, required_source="top.q")), "source_only")
        for bad in (
            {"expected_rule": "CST-009"},                                             # empty spec
            {"expected_rule": "CST-009", "required_message_contains": ""},
            {"expected_rule": "CST-009", "required_message_contains": "  "},
            {"expected_rule": "CST-009", "required_message_contains": ["x"]},
            {"expected_rule": "CST-009", "launch": "top.src", "required_message_contains": "x"},  # launch alone
            dict(design_level, endpoint="top.q", required_signals=["top.q"]),                 # mixed
            dict(design_level, endpoint="top.q", required_source="top.src"),
        ):
            with self.assertRaises(ValueError, msg=str(bad)):
                score.validate_match_spec(bad)
        op = _valid_crossing_op()
        for key in ("endpoint", "launch"):
            del op[key]
        op["required_message_contains"] = "set_clock_latency"
        score.validate_operator(op)

    def test_accept_unresolved_field_validation(self):
        score.validate_operator(_valid_crossing_op(accept_unresolved=True))
        score.validate_operator(_valid_crossing_op(accept_unresolved=False))
        score.validate_operator(_valid_crossing_op(accept_unresolved=True, require_incomplete_scope="constraints"))
        for bad in ({"accept_unresolved": "yes"}, {"accept_unresolved": 1},
                    {"require_incomplete_scope": "constraints"},
                    {"accept_unresolved": False, "require_incomplete_scope": "constraints"},
                    {"accept_unresolved": True, "require_incomplete_scope": ""}):
            with self.assertRaises(ValueError, msg=str(bad)):
                score.validate_operator(_valid_crossing_op(**bad))


class ExtendedScoringTests(unittest.TestCase):
    def test_required_message_contains_is_a_substring_filter(self):
        op = {"expected_rule": "CST-009", "required_message_contains": "set_clock_latency"}
        outcome, matched = score.score({"diagnostics": []}, _ok([_finding()]), op)
        self.assertEqual(outcome, "CAUGHT")
        self.assertEqual(matched["required_message_contains"], "set_clock_latency")
        self.assertEqual(matched["signals"], [])
        for bad in (_finding(message="unsupported SDC command 'set_clock_uncertainty'"),
                    _finding(message=""), _finding(rule_id="CST-010")):
            self.assertEqual(score.score({"diagnostics": []}, _ok([bad]), op)[0], "MISSED")
        diagnostic = {k: v for k, v in _finding().items() if k != "message"}
        self.assertEqual(score.score({"diagnostics": []}, _ok([diagnostic]), op)[0], "MISSED")
        # A baseline finding with the same fingerprint earns nothing.
        self.assertEqual(score.score({"diagnostics": [_finding()]}, _ok([_finding()]), op)[0], "MISSED")

    def test_message_filter_composes_with_signal_and_crossing_matching(self):
        d = _finding(rule_id="CDC-003", signals=["top.a", "top.b"], message="crossing top.a -> top.b has no relationship")
        for op in ({"expected_rule": "CDC-003", "endpoint": "top.b", "required_message_contains": "no relationship"},
                   {"expected_rule": "CDC-003", "required_signals": ["top.a"], "required_message_contains": "no relationship"}):
            self.assertEqual(score.score({"diagnostics": []}, _ok([d]), op)[0], "CAUGHT")
            self.assertEqual(score.score({"diagnostics": []}, _ok([d]), dict(op, required_message_contains="other"))[0], "MISSED")

    def test_unresolved_gates_only_when_explicitly_accepted(self):
        unresolved = _finding(confidence="unresolved")
        op = {"expected_rule": "CST-009", "required_message_contains": "set_clock_latency"}
        self.assertEqual(score.score({"diagnostics": []}, _ok([unresolved]), op)[0], "CAUGHT_NONGATING")
        self.assertEqual(score.score({"diagnostics": []}, _ok([unresolved]), dict(op, accept_unresolved=False))[0],
                         "CAUGHT_NONGATING")
        outcome, matched = score.score({"diagnostics": []}, _ok([unresolved]), dict(op, accept_unresolved=True))
        self.assertEqual(outcome, "CAUGHT")
        self.assertTrue(matched["accept_unresolved"])
        for severity in ("ERROR", "FATAL", "WARNING"):
            self.assertEqual(score.score({"diagnostics": []}, _ok([dict(unresolved, severity=severity)]),
                                         dict(op, accept_unresolved=True))[0], "CAUGHT")
        # INFO, not-analysed, waived and generic-obligation findings never gate, even when accepted.
        for weak in (dict(unresolved, severity="INFO"),
                     dict(unresolved, disposition="NOT_ANALYSED"),
                     dict(unresolved, disposition="WAIVED"),
                     dict(unresolved, disposition="OBLIGATION"),
                     dict(unresolved, rule_id="CDC-024")):
            weak_op = dict(op, expected_rule=weak["rule_id"], accept_unresolved=True)
            self.assertEqual(score.score({"diagnostics": []}, _ok([weak]), weak_op)[0], "CAUGHT_NONGATING", str(weak))

    def test_require_incomplete_scope_is_checked_against_the_mutant_report(self):
        unresolved = _finding(confidence="unresolved")
        op = {"expected_rule": "CST-009", "required_message_contains": "set_clock_latency",
              "accept_unresolved": True, "require_incomplete_scope": "constraints"}
        incomplete = [{"scope": "constraints", "analysis_coverage": "INCOMPLETE", "reasons": ["x"]}]
        legacy = [{"scope": "constraints", "coverage": "INCOMPLETE"}]
        for scopes in (incomplete, legacy):
            outcome, matched = score.score({"diagnostics": []}, _ok([unresolved], scopes), op)
            self.assertEqual(outcome, "CAUGHT")
            self.assertEqual(matched["require_incomplete_scope"], "constraints")
        for scopes in (
            None,
            [],
            [{"scope": "constraints", "analysis_coverage": "COMPLETE"}],
            [{"scope": "clock-relationships", "analysis_coverage": "INCOMPLETE"}],
            [{"scope": "constraints"}],
            "not-a-list",
        ):
            mutant = _ok([unresolved])
            if scopes is not None:
                mutant["coverage"] = {"scopes": scopes}
            self.assertEqual(score.score({"diagnostics": []}, mutant, op)[0], "CAUGHT_NONGATING", str(scopes))
        # Established findings gate regardless of the scope requirement.
        self.assertEqual(score.score({"diagnostics": []}, _ok([_finding()]), op)[0], "CAUGHT")


def _fake_binary(directory):
    """A stand-in sverity: logs argv, and reports findings keyed on marker text in its inputs."""
    path = pathlib.Path(directory) / "fake_sverity"
    path.write_text(f"""#!{sys.executable}
import json, os, sys
argv = sys.argv[1:]
log = os.path.join(os.path.dirname(os.path.abspath(__file__)), "argv.log")
open(log, "a").write(json.dumps([os.getcwd()] + argv) + "\\n")
out = argv[argv.index("--json") + 1]
sdc = open(argv[argv.index("--sdc") + 1]).read()
sources = " ".join(open(a).read() for a in argv if a.endswith((".sv", ".v")) and os.path.exists(a))
diagnostics, scopes = [], []
if "MARK_APPEND" in sdc or "MARK_REPLACED" in sdc:
    diagnostics.append(dict(fingerprint="sdc-new", rule_id="CST-009", severity="WARNING", confidence="unresolved",
                            message="unsupported SDC command MARK", signals=[]))
    scopes.append(dict(scope="constraints", analysis_coverage="INCOMPLETE", reasons=["unsupported"]))
if "MARK_RTL" in sources:
    diagnostics.append(dict(fingerprint="rtl-new", rule_id="CDC-008", severity="WARNING", confidence="established",
                            message="m", signals=["cc_cdc_2phase.x"]))
json.dump(dict(summary=dict(execution_status="SUCCESS"), diagnostics=diagnostics,
               coverage=dict(scopes=scopes)), open(out, "w"))
""")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)
    return path


class RunnerFlagTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = pathlib.Path(self.tmp.name)
        self.binary = _fake_binary(self.dir)
        self.manifest = json.loads(score.MANIFEST.read_text())
        self.spec = next(x for x in self.manifest["known_good"] if x["name"] == "cc_cdc_2phase")

    def argv_log(self):
        return [json.loads(line) for line in (self.dir / "argv.log").read_text().splitlines()]

    def test_default_invocation_is_unchanged(self):
        cwd = self.dir / "cwd"
        cwd.mkdir()
        score.run(self.binary, self.spec, cwd, self.dir / "reports", {"tech_cells_generic": "/dep"}, "t")
        argv = self.argv_log()[0][1:]
        self.assertEqual(argv[argv.index("--goal"):argv.index("--goal") + 4], ["--goal", "cdc", "--top", "cc_cdc_2phase"])
        self.assertNotIn("--profile", argv)
        self.assertNotIn("--deck", argv)
        self.assertEqual(argv[argv.index("--sdc") + 1], str(score.ROOT / "tests/public_projects" / self.spec["sdc"]))

    def test_profile_deck_and_sdc_are_passed_only_when_given(self):
        cwd = self.dir / "cwd"
        cwd.mkdir()
        sdc = self.dir / "custom.sdc"
        sdc.write_text("create_clock -name c -period 1 [get_ports c]\n")
        score.run(self.binary, self.spec, cwd, self.dir / "reports", {"tech_cells_generic": "/dep"}, "t",
                  profile="asic", deck="cdc", sdc=sdc)
        argv = self.argv_log()[0][1:]
        self.assertEqual(argv[argv.index("--profile") + 1], "asic")
        self.assertEqual(argv[argv.index("--deck") + 1], "cdc")
        self.assertEqual(argv[argv.index("--sdc") + 1], str(sdc))

    def test_active_sdc_and_label_never_embed_a_local_path(self):
        self.assertEqual(score.active_sdc(self.spec), score.ROOT / "tests/public_projects" / self.spec["sdc"])
        self.assertEqual(score.sdc_label(score.active_sdc(self.spec), self.spec), self.spec["sdc"])
        with self.assertRaises(RuntimeError):
            score.active_sdc(self.spec, self.dir)
        (self.dir / "cc_cdc_2phase.sdc").write_text("x\n")
        outside = score.active_sdc(self.spec, self.dir)
        self.assertEqual(score.sdc_label(outside, self.spec, self.dir), "cc_cdc_2phase.sdc")
        reviewed = score.ROOT / "tests/public_projects/reviewed_sdc"
        if (reviewed / "cc_cdc_2phase.sdc").is_file():
            self.assertEqual(score.sdc_label(score.active_sdc(self.spec, reviewed), self.spec, reviewed),
                             "tests/public_projects/reviewed_sdc/cc_cdc_2phase.sdc")


class MainEndToEndTests(unittest.TestCase):
    """Drive main() with a fake sverity and fake checkouts (no network, no real analysis)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = pathlib.Path(self.tmp.name)
        self.binary = _fake_binary(self.dir)
        self.manifest = json.loads(score.MANIFEST.read_text())
        self.repos = {x["name"]: x for x in self.manifest["repositories"]}
        self.checkout_root = self.dir / "checkouts"
        paths = {}
        for spec in self.manifest["known_good"]:
            repo = self.checkout_root / spec["repository"]
            (repo / spec["workdir"]).mkdir(parents=True, exist_ok=True)
            for source in spec["sources"]:
                (repo / spec["workdir"] / source).write_text("module m; // RTL_ORIGINAL\nendmodule\n")
            paths[spec["repository"]] = repo
            for dep in spec.get("dependencies", []):
                paths[dep] = self.checkout_root / dep
                paths[dep].mkdir(parents=True, exist_ok=True)
        self.paths = paths
        self.sdc_dir = self.dir / "sdc"
        self.sdc_dir.mkdir()
        for spec in self.manifest["known_good"]:
            (self.sdc_dir / (spec["name"] + ".sdc")).write_text(
                "create_clock -name a -period 10 [get_ports a]\nset_clock_groups -asynchronous -group {a}\n")
        self.operators = self.dir / "ops.json"

    def main(self, operators, *flags):
        self.operators.write_text(json.dumps({"candidates": operators}))
        out = self.dir / ("out-%d.json" % len(list(self.dir.glob("out-*.json"))))
        argv = ["cdc_mutation_score.py", str(self.binary), str(self.dir / "scratch"), "--json", str(out),
                "--operators", str(self.operators), *flags]
        with mock.patch.object(score, "checkouts", return_value=(self.paths, self.repos)), \
                mock.patch.object(sys, "argv", argv), contextlib.redirect_stdout(io.StringIO()):
            rc = score.main()
        return rc, json.loads(out.read_text())

    def test_sdc_mutation_with_profile_deck_and_sdc_dir(self):
        append = _sdc_op(id="append-unsupported", old=None, new=None, line_anchor=None, file="active.sdc",
                         expected_rule="CST-009", sdc_append="MARK_APPEND set_clock_latency 1 [get_clocks a]",
                         required_signals=None, required_message_contains="unsupported SDC command",
                         accept_unresolved=True, require_incomplete_scope="constraints")
        replace = _sdc_op(id="replace-groups", file="cc_cdc_2phase.sdc", old="-asynchronous", new="-physically_exclusive MARK_REPLACED",
                          line_anchor="set_clock_groups", target_line=2, expected_rule="CST-009",
                          required_signals=None, required_message_contains="unsupported SDC command")
        rc, out = self.main([append, replace], "--profile", "asic", "--deck", "cdc", "--sdc-dir", str(self.sdc_dir))
        self.assertEqual(rc, 0)
        self.assertEqual([m["outcome"] for m in out["mutants"]], ["CAUGHT", "CAUGHT_NONGATING"])
        for m in out["mutants"]:
            self.assertEqual(m["target"], "sdc")
            self.assertNotEqual(m["original_sdc_sha256"], m["mutant_sdc_sha256"])
            self.assertEqual(m["source_path"], "cc_cdc_2phase.sdc")
        self.assertEqual(out["mutants"][0]["accept_unresolved"], True)
        self.assertEqual(out["mutants"][0]["require_incomplete_scope"], "constraints")
        self.assertEqual(out["mutants"][1]["line"], 2)
        design = out["designs"]["cc_cdc_2phase"]
        self.assertEqual(design["invocation"]["profile"], "asic")
        self.assertEqual(design["invocation"]["deck"], "cdc")
        self.assertEqual(design["invocation"]["sdc"], "cc_cdc_2phase.sdc")
        self.assertEqual(out["options"], {"profile": "asic", "deck": "cdc", "sdc_dir": "sdc"})
        text = json.dumps(out)
        self.assertNotIn(self.tmp.name, text)
        self.assertNotIn("/" + "home/", text)
        # Paired baseline and mutant ran on the same scratch SDC path with the flags applied.
        runs = [json.loads(line) for line in (self.dir / "argv.log").read_text().splitlines()]
        mutant_runs = [r for r in runs if any("mutants" in a for a in r)]
        self.assertEqual(len(mutant_runs), 4)
        for before, after in zip(mutant_runs[0::2], mutant_runs[1::2]):
            sdc = lambda r: r[r.index("--sdc") + 1]
            self.assertEqual(sdc(before), sdc(after))
            self.assertTrue(sdc(before).endswith("active.sdc"))
            self.assertIn("--profile", after)
            self.assertIn("--deck", after)
            self.assertIn("-baseline", before[before.index("--json") + 1])
        # Only the mutated copy changed: the reviewed SDC directory is untouched.
        self.assertNotIn("MARK", (self.sdc_dir / "cc_cdc_2phase.sdc").read_text())
        self.assertEqual(out["mutants"][0]["original_sdc_sha256"], design["invocation"]["sdc_sha256"])

    def test_sdc_file_label_must_name_the_active_sdc(self):
        op = _sdc_op(file="other.sdc", old=None, new=None, line_anchor=None, sdc_append="MARK_APPEND",
                     expected_rule="CST-009", required_signals=None, required_message_contains="x")
        with self.assertRaises(RuntimeError):
            self.main([op], "--sdc-dir", str(self.sdc_dir))

    def test_default_invocation_and_rtl_operator_are_backward_compatible(self):
        rtl = {"id": "rtl-mark", "design": "cc_cdc_2phase", "file": "cc_cdc_2phase.sv", "old": "RTL_ORIGINAL",
               "new": "MARK_RTL", "line_anchor": "module m;", "defect": "d", "precondition": "p",
               "expected_rule": "CDC-008", "endpoint": "cc_cdc_2phase.x"}
        rc, out = self.main([rtl])
        self.assertEqual(rc, 0)
        self.assertEqual(out["mutants"][0]["outcome"], "CAUGHT")
        self.assertNotIn("target", out["mutants"][0])
        self.assertNotIn("options", out)
        self.assertNotIn("profile", out["designs"]["cc_cdc_2phase"]["invocation"])
        self.assertNotIn("deck", out["designs"]["cc_cdc_2phase"]["invocation"])
        self.assertEqual(out["designs"]["cc_cdc_2phase"]["invocation"]["sdc"], "common_cells.sdc")
        runs = [json.loads(line) for line in (self.dir / "argv.log").read_text().splitlines()]
        self.assertTrue(all("--profile" not in r and "--deck" not in r for r in runs))
        self.assertEqual(out["mutants"][0]["source_path"], "src/cc_cdc_2phase.sv")

    def test_same_fingerprint_severity_escalation_is_fresh(self):
        op = {"expected_rule": "CDC-015", "required_signals": ["top.req_q", "top.ack_q"]}
        base_info = {"diagnostics": [
            {"fingerprint": "same-fp", "rule_id": "CDC-015", "severity": "INFO",
             "confidence": "heuristic", "signals": ["top.req_q", "top.ack_q"]},
        ]}
        esc_warn = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": [
            {"fingerprint": "same-fp", "rule_id": "CDC-015", "severity": "WARNING",
             "confidence": "heuristic", "signals": ["top.req_q", "top.ack_q"]},
        ]}
        self.assertEqual(score.score(base_info, esc_warn, op)[0], "CAUGHT")

        base_warn = {"diagnostics": [
            {"fingerprint": "same-fp", "rule_id": "CDC-015", "severity": "WARNING",
             "confidence": "heuristic", "signals": ["top.req_q", "top.ack_q"]},
        ]}
        esc_err = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": [
            {"fingerprint": "same-fp", "rule_id": "CDC-015", "severity": "ERROR",
             "confidence": "established", "signals": ["top.req_q", "top.ack_q"]},
        ]}
        self.assertEqual(score.score(base_warn, esc_err, op)[0], "CAUGHT")
        # Same or lower severity with identical fingerprint remains non-fresh (MISSED).
        self.assertEqual(score.score(base_warn, esc_warn, op)[0], "MISSED")
        self.assertEqual(score.score({"diagnostics": esc_err["diagnostics"]}, esc_err, op)[0], "MISSED")
        deescalate = {"summary": {"execution_status": "SUCCESS"}, "diagnostics": base_info["diagnostics"]}
        self.assertEqual(score.score(base_warn, deescalate, op)[0], "MISSED")


if __name__ == "__main__":
    unittest.main()
