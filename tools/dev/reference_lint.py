#!/usr/bin/env python3
"""Replays the lint workflow's reference flow (docs/rules/LINT_WORKFLOW.md) on pinned OpenTitan IPs.

    reference_lint.py SVERITY SCRATCH [--ip uart --ip aes] [--max-gb 8]

For each IP: FuseSoC sets up the IP's own lint target (the files and Verilator configurations OpenTitan lints it
with), migrate_verilator.py turns that IP's configurations into SVerity waivers, and SVerity lints the IP with them.
The residual findings are what a team switching tools would have to triage. Per IP it prints, and writes to
SCRATCH/<ip>/summary.json: open WARNING/ERROR findings per rule, waived and INFO counts, Unresolved findings, the
nonblank, noncomment source lines analysed, and open findings per 1,000 of those lines. It changes nothing in the
repository; the checkout and every output live in SCRATCH (the same layout as capacity.py, so a checkout is shared).
"""
import argparse
import collections
import importlib.util
import json
import pathlib
import re
import subprocess
import sys

import capacity

_MG_SPEC = importlib.util.spec_from_file_location("mem_guard", pathlib.Path(__file__).resolve().with_name("mem_guard.py"))
mem_guard = importlib.util.module_from_spec(_MG_SPEC)
_MG_SPEC.loader.exec_module(mem_guard)

HERE = pathlib.Path(__file__).resolve().parent
PIN = json.loads((HERE.parents[1] / "tests/public_projects/lint_reference.json").read_text())

FILELIST_SCRIPT = """
import json, pathlib, sys, yaml
eda = pathlib.Path(sys.argv[1]); out = pathlib.Path(sys.argv[2]); base = eda.parent
description = yaml.safe_load(eda.read_text())
includes, sources, configs = [], [], []
for entry in description["files"]:
    path = (base / entry["name"]).resolve()
    if entry.get("is_include_file"):
        directory = (base / entry.get("include_path", str(pathlib.Path(entry["name"]).parent))).resolve()
        if directory not in includes: includes.append(directory)
    elif entry.get("file_type") in ("systemVerilogSource", "verilogSource"):
        sources.append(path)
    elif entry.get("file_type") == "vlt":
        configs.append(path)
options = description.get("tool_options", {}).get("verilator", {}).get("verilator_options", [])
defines = [o[2:] for o in options if o.startswith("-D")]
out.write_text("\\n".join(["+define+SYNTHESIS"] + [f"+define+{d}" for d in defines] + [f"+incdir+{p}" for p in includes]
                         + [str(p) for p in sources]) + "\\n")
print(json.dumps({"top": description["toplevel"], "sources": [str(s) for s in sources],
                  "configs": [str(c) for c in configs]}))
"""


def source_lines(paths):
    """Nonblank lines that are not only a comment (block comments are followed across lines)."""
    total = 0
    for path in paths:
        text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), pathlib.Path(path).read_text(errors="replace"), flags=re.S)
        total += sum(1 for line in text.splitlines() if line.strip() and not line.strip().startswith("//"))
    return total


def setup(ip, scratch, repo, venv):
    build = scratch / "lint" / ip
    if not list(build.glob("*/lint-verilator/*.eda.yml")):
        capacity.run(venv / "bin/fusesoc", f"--cores-root={repo}", "run", "--setup", f"--target={PIN['target']}",
                     "--tool=verilator", f"--mapping={PIN['mapping']}", f"--build-root={build}", f"lowrisc:ip:{ip}")
    eda = next(build.glob("*/lint-verilator/*.eda.yml"))
    filelist = build / "design.f"
    done = subprocess.run([venv / "bin/python", "-c", FILELIST_SCRIPT, eda, filelist], check=True, capture_output=True, text=True)
    return filelist, json.loads(done.stdout)


def replay(binary, ip, scratch, repo, venv, max_gb):
    try:
        filelist, design = setup(ip, scratch, repo, venv)
    except subprocess.CalledProcessError as failure:
        return {"ip": ip, "execution": f"SETUP FAILED: {failure.cmd[-1]} exited {failure.returncode}"}
    out = scratch / "lint" / ip
    waivers, migration, config = out / "waivers.json", out / "migration.md", out / "config.json"
    subprocess.run([sys.executable, HERE / "migrate_verilator.py", *design["configs"], "--root", repo, "--waivers", waivers,
                    "--report", migration, "--config", config, "--owner", "opentitan-verilator-policy"], check=True, capture_output=True)
    report = out / "report.json"
    report.unlink(missing_ok=True)
    mem_max = f"{max(1, int(round(max_gb * 1024)))}M"
    command, unit = mem_guard.build_command(
        ["timeout", "--kill-after=5s", "1800", str(binary), "--single-unit", "--goal", "lint", "--top", design["top"],
         "-f", str(filelist), "--config", str(config), "--waivers", str(waivers), "--root", str(repo),
         "--json", str(report), "--max-console", "1"],
        profile="capacity", label=f"ref-lint-{ip}", mem_max=mem_max, exec_mode=True,
    )
    done = subprocess.run(command, capture_output=True, text=True)
    oom, detail = mem_guard.inspect_oom(unit, done.stderr, done.returncode,
                                        profile="capacity", label=f"ref-lint-{ip}", mem_max=mem_max)
    row = {"ip": ip, "top": design["top"], "exit": 137 if oom else done.returncode, "configs": len(design["configs"])}
    if oom:
        row["execution"] = f"MEMORY LIMIT: {detail}"
        return row
    if not report.exists():
        row["execution"] = "NO REPORT: the run was stopped before it wrote one"
        return row
    data = json.loads(report.read_text())
    lines = source_lines(design["sources"])
    diagnostics = data["diagnostics"]
    open_ = [d for d in diagnostics if d["severity"] in ("WARNING", "ERROR", "FATAL") and d.get("disposition") != "WAIVED"]
    row.update(execution=data["summary"]["execution_status"], coverage=data["summary"]["analysis_coverage"], lines=lines,
               open=len(open_), per_kloc=round(1000 * len(open_) / lines, 1) if lines else None,
               unresolved=sum(1 for d in open_ if d.get("confidence") == "unresolved"),
               waived=sum(1 for d in diagnostics if d.get("disposition") == "WAIVED"),
               info=sum(1 for d in diagnostics if d["severity"] == "INFO"),
               rules=dict(collections.Counter(d["rule_id"] for d in open_).most_common()))
    (out / "summary.json").write_text(json.dumps(row, indent=1) + "\n")
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("scratch", type=pathlib.Path)
    parser.add_argument("--ip", action="append")
    parser.add_argument("--max-gb", type=float, default=8.0)
    args = parser.parse_args()
    scratch = args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    if PIN["commit"] != capacity.pin["commit"]:
        sys.exit("lint_reference.json and capacity.json pin different OpenTitan commits")
    repo = capacity.checkout(scratch)
    venv, _ = capacity.setup_fusesoc(scratch, repo)
    for ip in args.ip or PIN["ips"]:
        print(json.dumps(replay(args.binary.resolve(), ip, scratch, repo, venv, args.max_gb)), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
