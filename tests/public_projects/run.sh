#!/usr/bin/env bash
# Clones each pinned project from tests/public_projects/manifest.json into a scratch dir and runs sverity on it.
# Usage: tests/public_projects/run.sh [path/to/sverity] [scratch dir]
# Dev tooling only (uses python3 to read the manifest); the sverity binary itself has no Python dependency.
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
sv="$(realpath "${1:-$here/build/sverity}")"
scratch="${2:-$(mktemp -d)}"
python3 -I - "$here/tests/public_projects/manifest.json" "$sv" "$scratch" <<'PY'
import json, subprocess, sys, time, os
manifest, sv, scratch = sys.argv[1:4]
fail = 0
for p in json.load(open(manifest))["projects"]:
    d = os.path.join(scratch, p["name"])
    if not os.path.isdir(d):
        subprocess.run(["git", "clone", "-q", p["url"], d], check=True)
    subprocess.run(["git", "-C", d, "checkout", "-q", p["commit"]], check=True)
    out = os.path.join(scratch, p["name"] + ".json")
    t = time.time()
    subprocess.run([sv, "--no-color", "--json", out, "--exit-policy", "violations"] + p["args"], cwd=d,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dt = time.time() - t
    rep = json.load(open(out)); e = p["expect"]
    ok = rep["summary"]["execution_status"] == e["execution_status"] and dt <= e["max_seconds"] and \
         rep["coverage"]["design_objects"].get("register_bits", 0) >= e.get("min_register_bits", 0)
    print(("OK   " if ok else "FAIL ") + p["name"], "%.1fs" % dt, rep["summary"]["analysis_coverage"], rep["summary"]["review_status"])
    fail += 0 if ok else 1
sys.exit(1 if fail else 0)
PY
