#!/usr/bin/env python3
"""Measures SVerity on the pinned OpenTitan design: the capacity target of the 1.0 criteria.

Not part of tools/dev/check.sh, because it needs a large checkout and several GB of memory. External RTL stays in scratch
space and is never vendored. Usage:

    capacity.py SVERITY SCRATCH [--top aes --top top_earlgrey] [--max-gb 8] [--min-available-gb 8]
    capacity.py SVERITY SCRATCH --top top_earlgrey --profile asic --goal cdc --deck cdc --sdc REVIEWED.sdc --timeout-seconds 600 --max-gb 16 --exit-policy strict

Each run has an address-space limit, so running out of memory ends in a reported failure rather than in the host's
out-of-memory handling killing something else. A run starts only if the machine has the stated memory available.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import pathlib
import platform
import subprocess
import sys
import time

_MG_SPEC = importlib.util.spec_from_file_location("mem_guard", pathlib.Path(__file__).resolve().with_name("mem_guard.py"))
mem_guard = importlib.util.module_from_spec(_MG_SPEC)
_MG_SPEC.loader.exec_module(mem_guard)

here = pathlib.Path(__file__).resolve().parents[2]
pin = json.loads((here / "tests/public_projects/capacity.json").read_text())


def run(*args, cwd=None, stdout=None):
    subprocess.run([str(a) for a in args], cwd=cwd, stdout=stdout, check=True)


def available_gb():
    for line in pathlib.Path("/proc/meminfo").read_text().splitlines():
        if line.startswith("MemAvailable:"):
            return int(line.split()[1]) / 1024 / 1024
    return 0.0


def checkout(scratch, *, stdout=None):
    repo = scratch / "opentitan"
    if not repo.is_dir():
        run("git", "clone", "--filter=blob:none", "--no-checkout", pin["url"], repo, stdout=stdout)
        run("git", "sparse-checkout", "set", *pin["sparse_paths"], cwd=repo, stdout=stdout)
    run("git", "checkout", "--quiet", pin["commit"], cwd=repo, stdout=stdout)
    actual = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True, check=True).stdout.strip()
    if actual != pin["commit"]:
        sys.exit(f"opentitan is at {actual}, expected {pin['commit']}")
    return repo


def setup_fusesoc(scratch, repo, *, stdout=None):
    venv = scratch / "venv"
    if not venv.is_dir():
        run(sys.executable, "-m", "venv", venv, stdout=stdout)
        run(venv / "bin/pip", "install", "--quiet", f"fusesoc=={pin['fusesoc']}", "pyyaml", stdout=stdout)
    build = scratch / "build"
    if not list(build.glob("*/lint-verilator/*.eda.yml")):
        run(venv / "bin/fusesoc", f"--cores-root={repo}", "run", "--setup", "--target=lint", "--tool=verilator",
            f"--build-root={build}", pin["core"], stdout=stdout)
    return venv, next(build.glob("*/lint-verilator/*.eda.yml"))


FILELIST_SCRIPT = """
import pathlib, sys, yaml
eda = pathlib.Path(sys.argv[1]); out = pathlib.Path(sys.argv[2]); base = eda.parent
description = yaml.safe_load(eda.read_text())
includes, sources = [], []
for entry in description["files"]:
    path = (base / entry["name"]).resolve()
    if entry.get("is_include_file"):
        directory = (base / entry.get("include_path", str(pathlib.Path(entry["name"]).parent))).resolve()
        if directory not in includes: includes.append(directory)
    elif entry.get("file_type") in ("systemVerilogSource", "verilogSource"):
        sources.append(path)
out.write_text("\\n".join(["+define+SYNTHESIS"] + [f"+incdir+{p}" for p in includes] + [str(p) for p in sources]) + "\\n")
print(f"{len(sources)} source files, {len(includes)} include directories")
"""


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def measure(binary, filelist, top, scratch, max_gb, timeout_seconds, goal, constraints, exit_policy=None, deck=None, profile=None, libraries=()):
    report, timings = scratch / f"{top}.json", scratch / f"{top}.timings.json"
    mem_max = f"{max(1, int(round(max_gb * 1024)))}M"
    analyzer = [str(binary), "--single-unit", "--top", top, "-f", str(filelist), "--root", str(scratch),
                "--json", str(report), "--timings", str(timings), "--max-console", "1"]
    if goal:
        analyzer += ["--goal", goal]
    if deck:
        analyzer += ["--deck", deck]
    if profile:
        analyzer += ["--profile", profile]
    if exit_policy:
        analyzer += ["--exit-policy", exit_policy]
    for sdc in constraints:
        analyzer += ["--sdc", str(sdc)]
    for library in libraries:
        analyzer += ["--library", str(library)]
    command, unit = mem_guard.build_command(
        ["timeout", "--kill-after=5s", str(timeout_seconds), *analyzer],
        profile="capacity", label=top, mem_max=mem_max, exec_mode=True,
    )
    report.unlink(missing_ok=True)
    timings.unlink(missing_ok=True)
    started = time.monotonic()
    done = subprocess.run(command, capture_output=True, text=True)
    oom, detail = mem_guard.inspect_oom(unit, done.stderr, done.returncode,
                                        profile="capacity", label=top, mem_max=mem_max)
    row = {
        "top": top, "goal": goal, "deck": deck, "profile": profile, "exit_policy": exit_policy, "exit": 137 if oom else done.returncode,
        "seconds": round(time.monotonic() - started, 1),
        "limits": {"memory_max_gib": max_gb, "swap_max": 0, "timeout_seconds": timeout_seconds},
        "host": {"platform": platform.platform(), "processor": platform.processor()},
        "inputs": {
            "opentitan_commit": pin["commit"],
            "binary_sha256": digest(binary),
            "filelist_sha256": digest(filelist),
            "sdc_sha256": [digest(sdc) for sdc in constraints],
            "library_sha256": [digest(library) for library in libraries],
        },
    }

    def read_artifact(path, kind):
        if not path.exists():
            return None
        try:
            return json.loads(path.read_text())
        except (OSError, ValueError) as error:
            row[f"{kind}_error"] = str(error)
            return None

    data = None if oom else read_artifact(report, "report")
    if oom:
        row["oom_killed"] = True
        row["execution"] = f"MEMORY LIMIT: {detail}"
    elif data is not None:
        summary = data["summary"]
        row.update(execution=summary["execution_status"], coverage=summary["analysis_coverage"], review=summary["review_status"],
                   findings=summary["severity_counts"], objects=data["coverage"].get("design_objects", {}),
                   report_sha256=digest(report))
    else:
        row["execution"] = ("TIMEOUT: no report" if done.returncode == 124
                            else "NO REPORT: the run was stopped before it wrote one")
    measured = read_artifact(timings, "timings")
    if measured is not None:
        phase_peak = max((p["peak_rss_kb"] for p in measured["phases"]), default=0)
        checkpoint_peak = measured.get("counts", {}).get("rss_kb_checkpoint", 0)
        row["peak_gb"] = round(max(phase_peak, checkpoint_peak) / 1024 / 1024, 2)
        row["slowest"] = [[p["phase"], round(p["seconds"], 1)]
                          for p in sorted(measured["phases"], key=lambda p: -p["seconds"])[:4]]
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("scratch", type=pathlib.Path)
    parser.add_argument("--top", action="append")
    parser.add_argument("--max-gb", type=float, default=8.0)
    parser.add_argument("--min-available-gb", type=float, default=8.0)
    parser.add_argument("--timeout-seconds", type=int, default=3600)
    parser.add_argument("--goal", choices=("cdc", "rdc", "lint"))
    parser.add_argument("--deck", choices=("core", "domain", "1.0", "lint", "cdc"))
    parser.add_argument("--profile", choices=("generic", "asic", "fpga"))
    parser.add_argument("--exit-policy", choices=("strict",),
                        help="forward strict coverage and review exit semantics to the analyzer")
    parser.add_argument("--sdc", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--library", type=pathlib.Path, action="append", default=[], help="reviewed library cell description")
    args = parser.parse_args()
    if args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive")
    if not all(math.isfinite(value) and value > 0 for value in (args.max_gb, args.min_available_gb)):
        parser.error("memory limits must be finite and positive")
    constraints = [path.resolve() for path in args.sdc]
    for path in constraints:
        if not path.is_file():
            parser.error(f"SDC does not exist: {path}")
    libraries = [path.resolve() for path in args.library]
    for path in libraries:
        if not path.is_file():
            parser.error(f"library does not exist: {path}")
    required_gb = max(args.min_available_gb, args.max_gb)
    if available_gb() < required_gb:
        sys.exit(f"stop: under {required_gb} GiB of memory is available; a run could disturb other processes")
    scratch = args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    repo = checkout(scratch)
    venv, eda = setup_fusesoc(scratch, repo)
    filelist = scratch / "design.f"
    run(venv / "bin/python", "-c", FILELIST_SCRIPT, eda, filelist)
    for top in args.top or pin["tops"]:
        if available_gb() < required_gb:
            sys.exit(f"stop: under {required_gb} GiB of memory is available; a run could disturb other processes")
        print(json.dumps(measure(args.binary.resolve(), filelist, top, scratch, args.max_gb,
                                 args.timeout_seconds, args.goal, constraints, args.exit_policy, args.deck,
                                 args.profile, libraries)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
