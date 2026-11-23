#!/usr/bin/env python3
"""Run the model-only clock inventory against the pinned OpenTitan capacity filelist.

Usage: clock_inventory.py INVENTORY_BINARY SCRATCH --sdc REVIEWED_OR_CANDIDATE.sdc
The stable inventory is written to stdout; run limits and host measurements go to stderr.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import pathlib
import platform
import shutil
import subprocess
import sys
import time

import capacity

_MG_SPEC = importlib.util.spec_from_file_location("mem_guard", pathlib.Path(__file__).resolve().with_name("mem_guard.py"))
mem_guard = importlib.util.module_from_spec(_MG_SPEC)
_MG_SPEC.loader.exec_module(mem_guard)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("scratch", type=pathlib.Path)
    parser.add_argument("--top", default="top_earlgrey", choices=capacity.pin["tops"])
    parser.add_argument("--sdc", required=True, type=pathlib.Path)
    parser.add_argument("--library", action="append", default=[], type=pathlib.Path, help="reviewed library cell description")
    parser.add_argument("--max-gb", type=float, default=16.0)
    parser.add_argument("--min-available-gb", type=float, default=16.0)
    parser.add_argument("--min-free-gb", type=float, default=20.0)
    parser.add_argument("--timeout-seconds", type=int, default=600)
    parser.add_argument("--metadata", type=pathlib.Path, help="write non-deterministic host and timing metadata here")
    args = parser.parse_args()
    if args.timeout_seconds <= 0 or not all(math.isfinite(x) and x > 0 for x in
                                           (args.max_gb, args.min_available_gb, args.min_free_gb)):
        parser.error("timeout and resource limits must be positive; memory limits must be finite")
    binary = args.binary.resolve()
    sdc = args.sdc.resolve()
    if not binary.is_file():
        parser.error(f"inventory executable does not exist: {binary}")
    if not sdc.is_file():
        parser.error(f"SDC does not exist: {sdc}")
    scratch = args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    available = capacity.available_gb()
    free = shutil.disk_usage(scratch).free / (1024 ** 3)
    required = max(args.max_gb, args.min_available_gb)
    if available < required:
        sys.exit(f"stop: {available:.1f} GiB memory available, need {required:.1f} GiB")
    if free < args.min_free_gb:
        sys.exit(f"stop: {free:.1f} GiB free in {scratch}, need {args.min_free_gb:.1f} GiB")

    repo = capacity.checkout(scratch, stdout=sys.stderr)
    venv, eda = capacity.setup_fusesoc(scratch, repo, stdout=sys.stderr)
    filelist = scratch / "design.f"
    filelist_result = subprocess.run([str(venv / "bin/python"), "-c", capacity.FILELIST_SCRIPT,
                                      str(eda), str(filelist)], capture_output=True, text=True, check=True)
    if capacity.available_gb() < required:
        sys.exit(f"stop: under {required:.1f} GiB of memory is available before model construction")
    if shutil.disk_usage(scratch).free / (1024 ** 3) < args.min_free_gb:
        sys.exit(f"stop: under {args.min_free_gb:.1f} GiB disk space remains before model construction")

    mem_max = f"{max(1, int(round(args.max_gb * 1024)))}M"
    command, unit = mem_guard.build_command(
        ["timeout", "--kill-after=5s", str(args.timeout_seconds), str(binary), "--top", args.top,
         "-f", str(filelist), "--sdc", str(sdc), *[arg for lib in args.library for arg in ("--library", str(lib.resolve()))]],
        profile="capacity", label=f"clock-inventory-{args.top}", mem_max=mem_max, exec_mode=True,
    )
    started = time.monotonic()
    done = subprocess.run(command, capture_output=True, text=True)
    elapsed = round(time.monotonic() - started, 1)
    if done.returncode:
        oom, detail = mem_guard.inspect_oom(
            unit, done.stderr, done.returncode, profile="capacity",
            label=f"clock-inventory-{args.top}", mem_max=mem_max,
        )
        if oom:
            sys.exit(f"inventory failed (MEMORY LIMIT, {elapsed:.1f}s): {detail}")
        sys.stderr.write(done.stderr)
        sys.exit(f"inventory failed (exit {done.returncode}, {elapsed:.1f}s)")
    try:
        inventory = json.loads(done.stdout)
    except ValueError as error:
        sys.exit(f"inventory emitted invalid JSON: {error}")
    # Stable output contains only the model and parsed declarations. The sidecar records how it was obtained.
    print(json.dumps(inventory, sort_keys=True, separators=(",", ":")))
    metadata = {
        "tool": str(binary), "top": args.top, "opentitan_url": capacity.pin["url"],
        "opentitan_commit": capacity.pin["commit"], "core": capacity.pin["core"],
        "binary_sha256": sha256(binary),
        "filelist_sha256": sha256(filelist), "sdc_sha256": sha256(sdc),
        "filelist_summary": filelist_result.stdout.strip(),
        "sdc_path": str(sdc), "host": platform.platform(), "python": platform.python_version(),
        "memory_limit_gib": args.max_gb, "required_available_gib": required,
        "minimum_free_disk_gib": args.min_free_gb, "timeout_seconds": args.timeout_seconds,
        "available_memory_before_gib": round(available, 2), "free_disk_before_gib": round(free, 2),
        "elapsed_seconds": elapsed, "exit": done.returncode,
    }
    if args.metadata:
        args.metadata.resolve().write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
    else:
        print(json.dumps(metadata, sort_keys=True), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
