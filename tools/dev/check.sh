#!/usr/bin/env bash
# The one verification command: build, ctest, then the public-RTL gate. Stops at the first failure.
# Usage: tools/dev/check.sh [--no-gate]
# Environment overrides:
#   SVERITY_BUILD, SVERITY_SCRATCH
#   SVERITY_BUILD_JOBS / SVERITY_JOBS (safe default: 2, or 1 when MemAvailable < 6 GiB)
#   SVERITY_TEST_JOBS  / SVERITY_JOBS (safe default: 2, or 1 when MemAvailable < 4 GiB)
#   SVERITY_MEM_SLICE, SVERITY_SLICE_MEM_MAX (default: sverity.slice, 5G across all worktrees)
#   SVERITY_MEM_GUARD_MODE=auto|cgroup|rlimit|off
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cache="${XDG_CACHE_HOME:-$HOME/.cache}/sverity"
# Inside the checkout, so a build goes away with its worktree. CMake ties a build directory to its source path, so it cannot be shared.
build="${SVERITY_BUILD:-$root/build}"
scratch="${SVERITY_SCRATCH:-$cache/public}"
guard="$root/tools/dev/mem_guard.sh"

avail_gib=$(awk '/^MemAvailable:/ {print int($2 / 1048576)}' /proc/meminfo 2>/dev/null || echo "")
if [ -n "$avail_gib" ] && [ "${SVERITY_MEM_GUARD_MODE:-auto}" != "off" ] && [ "$avail_gib" -lt 2 ]; then
  echo "check: STOP — under 2 GiB MemAvailable (${avail_gib} GiB); refusing to start build/test" >&2
  exit 1
fi

# Heavy Release C++20 TUs (e.g. model.cpp, lint_model.cpp) use ~1.3–1.8 GiB each (~3.0 GiB at -j2).
# Explicit safe default is -j2 (-j1 under 6 GiB available) and never auto-scales above 2.
if [ -n "${SVERITY_BUILD_JOBS:-}" ]; then
  build_jobs="$SVERITY_BUILD_JOBS"
elif [ -n "${SVERITY_JOBS:-}" ]; then
  build_jobs="$SVERITY_JOBS"
elif [ -n "$avail_gib" ] && [ "$avail_gib" -lt 6 ]; then
  build_jobs=1
else
  build_jobs=2
fi

if [ -n "${SVERITY_TEST_JOBS:-}" ]; then
  test_jobs="$SVERITY_TEST_JOBS"
elif [ -n "${SVERITY_JOBS:-}" ]; then
  test_jobs="$SVERITY_JOBS"
elif [ -n "$avail_gib" ] && [ "$avail_gib" -lt 4 ]; then
  test_jobs=1
else
  test_jobs=2
fi

if [ "$build_jobs" -gt 2 ]; then
  echo "check: WARNING — build_jobs=$build_jobs exceeds safe default 2; heavy C++ TUs use ~1.3–1.8 GiB each and may hit the ${SVERITY_BUILD_MEM_MAX:-4G} build cgroup cap" >&2
fi
echo "check: build_jobs=$build_jobs test_jobs=$test_jobs (${avail_gib:-unknown} GiB available; slice=${SVERITY_MEM_SLICE:-sverity.slice} max=${SVERITY_SLICE_MEM_MAX:-5G})"

# CI keeps the whole log, because nobody can rerun a failure there by hand.
brief() { if [ -n "${CI:-}" ]; then cat; else tail -n "$1"; fi; }
[ -d "$root/third_party/slang/source" ] || git -C "$root" submodule update --init --recursive
# A new checkout then reuses what another one compiled, slang above all.
launcher=(-DCMAKE_CXX_COMPILER_LAUNCHER= -DCMAKE_C_COMPILER_LAUNCHER=)   # clears one a reused build directory remembers
if command -v ccache >/dev/null; then
  export CCACHE_BASEDIR="$root"
  # ccache cannot cache the scan for C++ modules, which this project does not use.
  launcher=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_SCAN_FOR_MODULES=OFF)
fi

run_guarded_brief() {
  local profile="$1" label="$2" tail_lines="$3"
  shift 3
  local rc=0
  set +e
  "$guard" --profile "$profile" --label "$label" -- "$@" | brief "$tail_lines"
  rc=${PIPESTATUS[0]}
  set -e
  if [ "$rc" -eq 137 ]; then
    echo "check: FAIL — $label exceeded memory limit (exit 137)" >&2
    return 137
  fi
  return "$rc"
}

run_guarded_brief configure cmake-configure 20 \
  cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "${launcher[@]}" >/dev/null
run_guarded_brief build cmake-build 1 \
  cmake --build "$build" -j"$build_jobs"
run_guarded_brief test ctest-driver 3 \
  ctest --test-dir "$build" -j"$test_jobs" --output-on-failure

if [ "${1:-}" != "--no-gate" ]; then
  rc=0
  set +e
  "$root/tests/public_projects/run.sh" "$build/sverity" "$scratch"
  rc=$?
  set -e
  if [ "$rc" -eq 137 ]; then
    echo "check: FAIL — public-RTL gate exceeded memory limit (exit 137)" >&2
    exit 137
  elif [ "$rc" -ne 0 ]; then
    exit "$rc"
  fi
fi
echo "check: PASS, binary at $build/sverity"
