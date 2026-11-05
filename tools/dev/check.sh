#!/usr/bin/env bash
# The one verification command: build, ctest, then the public-RTL gate. Stops at the first failure.
# Usage: tools/dev/check.sh [--no-gate]   (SVERITY_BUILD, SVERITY_SCRATCH and SVERITY_JOBS override the defaults)
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cache="${XDG_CACHE_HOME:-$HOME/.cache}/sverity"
# Inside the checkout, so a build goes away with its worktree. CMake ties a build directory to its source path, so it cannot be shared.
build="${SVERITY_BUILD:-$root/build}"
scratch="${SVERITY_SCRATCH:-$cache/public}"
jobs="${SVERITY_JOBS:-6}"
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
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "${launcher[@]}" >/dev/null
cmake --build "$build" -j"$jobs" | brief 1
ctest --test-dir "$build" -j"$jobs" --output-on-failure | brief 3
if [ "${1:-}" != "--no-gate" ]; then "$root/tests/public_projects/run.sh" "$build/sverity" "$scratch"; fi
echo "check: PASS, binary at $build/sverity"
