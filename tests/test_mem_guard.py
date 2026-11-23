#!/usr/bin/env python3
"""Unit tests for tools/dev/mem_guard.{sh,py} cgroup enforcement and OOM reporting."""
import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MEM_GUARD_SH = ROOT / "tools/dev/mem_guard.sh"
SPEC_MG = importlib.util.spec_from_file_location("mem_guard", ROOT / "tools/dev/mem_guard.py")
mem_guard = importlib.util.module_from_spec(SPEC_MG)
SPEC_MG.loader.exec_module(mem_guard)

SPEC_ACC = importlib.util.spec_from_file_location("accuracy", ROOT / "tools/dev/accuracy.py")
accuracy = importlib.util.module_from_spec(SPEC_ACC)
SPEC_ACC.loader.exec_module(accuracy)

FIXTURE_RUNNER = pathlib.Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None
TEST_SLICE = "sverity-test.slice"


def cgroup_v2_ready():
    if not shutil.which("systemd-run") or not shutil.which("systemctl"):
        return False
    controllers = pathlib.Path("/sys/fs/cgroup/cgroup.controllers")
    if not controllers.is_file() or "memory" not in controllers.read_text().split():
        return False
    probe = subprocess.run(
        ["systemctl", "--user", "show", TEST_SLICE, "-p", "MemoryAccounting", "--value"],
        capture_output=True, text=True, timeout=5,
    )
    return probe.returncode == 0 and probe.stdout.strip() == "yes"


def cleanup_test_slice():
    if shutil.which("systemctl"):
        subprocess.run(["systemctl", "--user", "revert", TEST_SLICE], capture_output=True, timeout=5)


class MemGuardTests(unittest.TestCase):
    def setUp(self):
        self.addCleanup(cleanup_test_slice)

    def test_per_invocation_exceeding_slice_rejected(self):
        proc = subprocess.run(
            ["bash", str(MEM_GUARD_SH), "--profile", "fixture", "--mem-max", "512M",
             "--slice", TEST_SLICE, "--slice-max", "256M", "--", sys.executable, "-c", "print('ran')"],
            capture_output=True, text=True, timeout=15,
        )
        self.assertEqual(proc.returncode, 2, proc.stdout + proc.stderr)
        self.assertIn("MEM_GUARD_CONFIG_ERROR", proc.stderr)
        self.assertNotIn("ran", proc.stdout)

    @unittest.skipUnless(FIXTURE_RUNNER and FIXTURE_RUNNER.is_file(), "requires sverity_fixture_runner path")
    def test_fixture_passes_under_guard(self):
        proc = subprocess.run(
            ["bash", str(MEM_GUARD_SH), "--profile", "fixture", "--label", "CDC-001",
             "--slice", TEST_SLICE, "--slice-max", "1G", "--",
             str(FIXTURE_RUNNER), str(ROOT / "tests/fixtures"), "CDC-001"],
            capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("[ OK ] CDC-001/", proc.stdout)

    @unittest.skipUnless(cgroup_v2_ready(), "requires systemd user cgroup v2 memory controller")
    def test_synthetic_oom_killed_and_reported_distinctly(self):
        proc = subprocess.run(
            ["bash", str(MEM_GUARD_SH), "--profile", "fixture", "--label", "synth-oom",
             "--mem-max", "64M", "--swap-max", "0", "--slice", TEST_SLICE, "--slice-max", "256M", "--",
             sys.executable, "-c",
             "b = bytearray(96 * 1024 * 1024)\nfor i in range(0, len(b), 4096): b[i] = 1\n"],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(proc.returncode, 137, proc.stdout + proc.stderr)
        self.assertIn("MEM_LIMIT_EXCEEDED:", proc.stderr)
        self.assertIn("cgroup OOM kill", proc.stderr)

    @unittest.skipUnless(cgroup_v2_ready(), "requires systemd user cgroup v2 memory controller")
    def test_shared_slice_aggregate_cap_enforced(self):
        script = (
            "import time\n"
            "b = bytearray(65 * 1024 * 1024)\n"
            "for i in range(0, len(b), 4096): b[i] = 1\n"
            "time.sleep(2)\n"
        )
        cmd_a = ["bash", str(MEM_GUARD_SH), "--profile", "fixture", "--label", "agg-a",
                 "--mem-max", "90M", "--swap-max", "0", "--slice", TEST_SLICE, "--slice-max", "100M",
                 "--", sys.executable, "-c", script]
        cmd_b = ["bash", str(MEM_GUARD_SH), "--profile", "fixture", "--label", "agg-b",
                 "--mem-max", "90M", "--swap-max", "0", "--slice", TEST_SLICE, "--slice-max", "100M",
                 "--", sys.executable, "-c", script]
        p1 = subprocess.Popen(cmd_a, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        p2 = subprocess.Popen(cmd_b, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        out1, err1 = p1.communicate(timeout=30)
        out2, err2 = p2.communicate(timeout=30)
        rcs = (p1.returncode, p2.returncode)
        errs = err1 + "\n" + err2
        self.assertIn(137, rcs, f"expected aggregate OOM kill in {rcs}\n{out1}\n{out2}\n{errs}")
        self.assertIn("MEM_LIMIT_EXCEEDED:", errs)

    @unittest.skipUnless(cgroup_v2_ready(), "requires systemd user cgroup v2 memory controller")
    def test_accuracy_reports_memory_limit_distinctly(self):
        with tempfile.TemporaryDirectory(prefix="sv-acc-oom-") as tmp:
            root = pathlib.Path(tmp)
            case = root / "cases/LNT/oom"
            case.mkdir(parents=True)
            (case / "design.sv").write_text("module top; endmodule\n")
            fake = root / "fake"
            fake.write_text(
                f"#!{sys.executable}\n"
                "b = bytearray(96 * 1024 * 1024)\n"
                "for i in range(0, len(b), 4096): b[i] = 1\n"
            )
            fake.chmod(0o755)
            label = dict(family="LNT", top="top", verdict="good", sites=[], author="a",
                         labels=[dict(by="r1", verdict="good"), dict(by="r2", verdict="good")])
            old = {k: os.environ.get(k) for k in ("SVERITY_ACCURACY_MEM_MAX", "SVERITY_MEM_SLICE", "SVERITY_SLICE_MEM_MAX")}
            os.environ["SVERITY_ACCURACY_MEM_MAX"] = "64M"
            os.environ["SVERITY_MEM_SLICE"] = TEST_SLICE
            os.environ["SVERITY_SLICE_MEM_MAX"] = "256M"
            try:
                res = accuracy.run(fake, case, label)
            finally:
                for k, v in old.items():
                    if v is None:
                        os.environ.pop(k, None)
                    else:
                        os.environ[k] = v
            self.assertEqual(res["execution"], "MEMORY LIMIT", res)
            self.assertIn("MEM_LIMIT_EXCEEDED:", res.get("reason") or "", res)


if __name__ == "__main__":
    unittest.main()
