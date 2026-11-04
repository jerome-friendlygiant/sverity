#!/usr/bin/env bash
# Dev-only runner; external RTL is cloned at pinned commits into scratch space.
# Usage: run.sh [sverity] [scratch] [--group all|robustness|corpus|self-check]
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
sv="$(realpath "${1:-$here/build/sverity}")"
scratch="${2:-$(mktemp -d)}"
shift "$(( $# < 2 ? $# : 2 ))"
exec python3 -I "$here/tests/public_projects/runner.py" --binary "$sv" --scratch "$scratch" "$@"
