#!/usr/bin/env python3
"""Measures SVerity on the pinned OpenTitan design: the capacity target of the 1.0 criteria.

Not part of tools/dev/check.sh, because it needs a large checkout and several GB of memory. External RTL stays in scratch
space and is never vendored. Usage:

    capacity.py SVERITY SCRATCH [--top aes --top top_earlgrey] [--max-gb 8] [--min-available-gb 8]

Each run has an address-space limit, so running out of memory ends in a reported failure rather than in the host's
out-of-memory handling killing something else. A run starts only if the machine has the stated memory available.
"""
import argparse
import json
import pathlib
import subprocess
import sys
import time

here = pathlib.Path(__file__).resolve().parents[2]
pin = json.loads((here / "tests/public_projects/capacity.json").read_text())


def run(*args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def available_gb():
    for line in pathlib.Path("/proc/meminfo").read_text().splitlines():
        if line.startswith("MemAvailable:"):
            return int(line.split()[1]) / 1024 / 1024
    return 0.0


def checkout(scratch):
    repo = scratch / "opentitan"
    if not repo.is_dir():
        run("git", "clone", "--filter=blob:none", "--no-checkout", pin["url"], repo)
        run("git", "sparse-checkout", "set", *pin["sparse_paths"], cwd=repo)
    run("git", "checkout", "--quiet", pin["commit"], cwd=repo)
    actual = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True, check=True).stdout.strip()
    if actual != pin["commit"]:
        sys.exit(f"opentitan is at {actual}, expected {pin['commit']}")
    return repo


def setup_fusesoc(scratch, repo):
    venv = scratch / "venv"
    if not venv.is_dir():
        run(sys.executable, "-m", "venv", venv)
        run(venv / "bin/pip", "install", "--quiet", f"fusesoc=={pin['fusesoc']}", "pyyaml")
    build = scratch / "build"
    if not list(build.glob("*/lint-verilator/*.eda.yml")):
        run(venv / "bin/fusesoc", f"--cores-root={repo}", "run", "--setup", "--target=lint", "--tool=verilator",
            f"--build-root={build}", pin["core"])
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


def measure(binary, filelist, top, scratch, max_gb):
    report, timings = scratch / f"{top}.json", scratch / f"{top}.timings.json"
    limit = int(max_gb * 1024 * 1024)
    command = (f"ulimit -v {limit}; exec timeout 3600 '{binary}' --single-unit --top {top} -f '{filelist}' --root '{scratch}' "
               f"--json '{report}' --timings '{timings}' --max-console 1")
    report.unlink(missing_ok=True)
    started = time.monotonic()
    done = subprocess.run(["bash", "-c", command], capture_output=True, text=True)
    row = {"top": top, "exit": done.returncode, "seconds": round(time.monotonic() - started, 1)}
    if report.exists():
        data = json.loads(report.read_text())
        summary = data["summary"]
        row.update(execution=summary["execution_status"], coverage=summary["analysis_coverage"], review=summary["review_status"],
                   findings=summary["severity_counts"], objects=data["coverage"].get("design_objects", {}))
    else:
        row["execution"] = "NO REPORT: the run was stopped before it wrote one"
    if timings.exists():
        measured = json.loads(timings.read_text())
        row["peak_gb"] = round(max((p["peak_rss_kb"] for p in measured["phases"]), default=0) / 1024 / 1024, 2)
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
    args = parser.parse_args()
    scratch = args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    repo = checkout(scratch)
    venv, eda = setup_fusesoc(scratch, repo)
    filelist = scratch / "design.f"
    run(venv / "bin/python", "-c", FILELIST_SCRIPT, eda, filelist)
    for top in args.top or pin["tops"]:
        if available_gb() < args.min_available_gb:
            sys.exit(f"stop: under {args.min_available_gb} GB of memory is available; a run could disturb other processes")
        print(json.dumps(measure(args.binary.resolve(), filelist, top, scratch, args.max_gb)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
