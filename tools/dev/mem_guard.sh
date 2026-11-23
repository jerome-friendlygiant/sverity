#!/usr/bin/env bash
# Kernel-enforced cgroup v2 RAM guard for SVerity builds, CTest runners, and dev/qualification tools.
# Usage:
#   tools/dev/mem_guard.sh [--profile PROFILE] [--mem-max SIZE] [--swap-max SIZE] \
#     [--slice NAME] [--slice-max SIZE] [--slice-swap-max SIZE] [--unit NAME] \
#     [--label TEXT] [--exec] -- COMMAND [ARGS...]
#
# Environment overrides:
#   SVERITY_MEM_GUARD_MODE=auto|cgroup|rlimit|off   (default: auto)
#   SVERITY_MEM_SLICE=<slice.slice>                 (default: sverity.slice)
#   SVERITY_SLICE_MEM_MAX=<SIZE>                    (default: 5G)
#   SVERITY_SLICE_SWAP_MAX=<SIZE>                   (default: 0)
#   SVERITY_MEM_SWAP_MAX=<SIZE>                     (default: 0)
#   SVERITY_<PROFILE>_MEM_MAX=<SIZE>                (e.g. SVERITY_BUILD_MEM_MAX=4G)
#   SVERITY_MEM_GUARD_QUIET=1                       (suppress fallback notice)
set -euo pipefail

profile="public"
mem_max=""
swap_max="${SVERITY_MEM_SWAP_MAX:-0}"
slice="${SVERITY_MEM_SLICE:-sverity.slice}"
slice_max="${SVERITY_SLICE_MEM_MAX:-5G}"
slice_swap="${SVERITY_SLICE_SWAP_MAX:-0}"
unit=""
label=""
exec_mode=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --profile) profile="${2:-}"; shift 2 ;;
    --mem-max) mem_max="${2:-}"; shift 2 ;;
    --swap-max) swap_max="${2:-}"; shift 2 ;;
    --slice) slice="${2:-}"; shift 2 ;;
    --slice-max) slice_max="${2:-}"; shift 2 ;;
    --slice-swap-max) slice_swap="${2:-}"; shift 2 ;;
    --unit) unit="${2:-}"; shift 2 ;;
    --label) label="${2:-}"; shift 2 ;;
    --exec) exec_mode=1; shift ;;
    --) shift; break ;;
    -h|--help)
      sed -n '2,15p' "$0"
      exit 0
      ;;
    *)
      echo "MEM_GUARD_CONFIG_ERROR: unknown option '$1' (expected '--' before command)" >&2
      exit 2
      ;;
  esac
done

if [ "$#" -eq 0 ]; then
  echo "MEM_GUARD_CONFIG_ERROR: missing command after '--'" >&2
  exit 2
fi

mode="${SVERITY_MEM_GUARD_MODE:-auto}"
if [ "$mode" = "off" ]; then
  exec "$@"
fi
case "$mode" in
  auto|cgroup|rlimit) ;;
  *)
    echo "MEM_GUARD_CONFIG_ERROR: invalid SVERITY_MEM_GUARD_MODE='$mode' (expected auto|cgroup|rlimit|off)" >&2
    exit 2
    ;;
esac

default_mem_for_profile() {
  case "$1" in
    fixture) echo "${SVERITY_FIXTURE_MEM_MAX:-512M}" ;;
    unit) echo "${SVERITY_UNIT_MEM_MAX:-1G}" ;;
    test) echo "${SVERITY_TEST_MEM_MAX:-1G}" ;;
    accuracy) echo "${SVERITY_ACCURACY_MEM_MAX:-1G}" ;;
    public) echo "${SVERITY_PUBLIC_MEM_MAX:-1G}" ;;
    robustness) echo "${SVERITY_ROBUSTNESS_MEM_MAX:-2G}" ;;
    configure) echo "${SVERITY_CONFIGURE_MEM_MAX:-1G}" ;;
    build) echo "${SVERITY_BUILD_MEM_MAX:-4G}" ;;
    capacity) echo "${SVERITY_CAPACITY_MEM_MAX:-4G}" ;;
    *) return 1 ;;
  esac
}

if [ -z "$mem_max" ]; then
  if ! mem_max="$(default_mem_for_profile "$profile")"; then
    echo "MEM_GUARD_CONFIG_ERROR: unknown profile '$profile'" >&2
    exit 2
  fi
fi

to_bytes() {
  local raw="${1,,}"
  if [ "$raw" = "infinity" ]; then
    echo "infinity"
    return 0
  fi
  if [[ ! "$raw" =~ ^([0-9]+([.][0-9]+)?)(b|k|kb|kib|m|mb|mib|g|gb|gib|t|tb|tib)?$ ]]; then
    return 1
  fi
  local num="${BASH_REMATCH[1]}"
  local unit_suffix="${BASH_REMATCH[3]}"
  awk -v v="$num" -v u="$unit_suffix" 'BEGIN {
    m = 1;
    if (u == "k" || u == "kb" || u == "kib") m = 1024;
    else if (u == "m" || u == "mb" || u == "mib") m = 1024 * 1024;
    else if (u == "g" || u == "gb" || u == "gib") m = 1024 * 1024 * 1024;
    else if (u == "t" || u == "tb" || u == "tib") m = 1024 * 1024 * 1024 * 1024;
    printf "%.0f\n", v * m;
  }'
}

if ! mem_max_bytes="$(to_bytes "$mem_max")" || [ "$mem_max_bytes" = "infinity" ] || [ "$mem_max_bytes" -le 0 ]; then
  echo "MEM_GUARD_CONFIG_ERROR: invalid positive --mem-max '$mem_max'" >&2
  exit 2
fi
if ! swap_max_bytes="$(to_bytes "$swap_max")"; then
  echo "MEM_GUARD_CONFIG_ERROR: invalid --swap-max '$swap_max'" >&2
  exit 2
fi
if ! slice_max_bytes="$(to_bytes "$slice_max")" || [ "$slice_max_bytes" = "0" ]; then
  echo "MEM_GUARD_CONFIG_ERROR: invalid --slice-max '$slice_max'" >&2
  exit 2
fi
if ! slice_swap_bytes="$(to_bytes "$slice_swap")"; then
  echo "MEM_GUARD_CONFIG_ERROR: invalid --slice-swap-max '$slice_swap'" >&2
  exit 2
fi

if [ "$slice_max_bytes" != "infinity" ] && [ "$mem_max_bytes" -gt "$slice_max_bytes" ]; then
  echo "MEM_GUARD_CONFIG_ERROR: requested --mem-max $mem_max exceeds shared slice budget $slice=$slice_max (set SVERITY_SLICE_MEM_MAX explicitly only after checking MemAvailable)" >&2
  exit 2
fi

cgroup_available() {
  [ "$mode" != "rlimit" ] || return 1
  command -v systemd-run >/dev/null 2>&1 || return 1
  command -v systemctl >/dev/null 2>&1 || return 1
  [ -r /sys/fs/cgroup/cgroup.controllers ] || return 1
  grep -qw memory /sys/fs/cgroup/cgroup.controllers || return 1
  local user_ctrl="/sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/cgroup.controllers"
  if [ -r "$user_ctrl" ]; then
    grep -qw memory "$user_ctrl" || return 1
  fi
  local acct
  acct="$(systemctl --user show "$slice" -p MemoryAccounting --value 2>/dev/null || true)"
  [ "$acct" = "yes" ]
}

if ! cgroup_available; then
  if [ "$mode" = "cgroup" ]; then
    echo "MEM_GUARD_UNAVAILABLE: systemd user cgroup v2 memory controller is unavailable" >&2
    exit 2
  fi
  limit_kb=$(( (mem_max_bytes + 1023) / 1024 ))
  [ "$limit_kb" -lt 1024 ] && limit_kb=1024
  if [ "${SVERITY_MEM_GUARD_QUIET:-0}" != "1" ]; then
    echo "mem_guard: cgroup v2 unavailable; falling back to ulimit -v ${limit_kb}K for profile=$profile" >&2
  fi
  ulimit -v "$limit_kb"
  exec "$@"
fi

if [ -z "$label" ]; then
  label="$(basename -- "$1")"
fi
safe_label="$(printf '%s' "$label" | tr -c 'A-Za-z0-9_.-' '_' | cut -c1-40)"
[ -z "$safe_label" ] && safe_label="run"
if [ -z "$unit" ]; then
  unit="sverity-${profile}-${safe_label}-$$-${RANDOM}"
fi
unit="${unit%.scope}"

cur_slice_max="$(systemctl --user show "$slice" -p MemoryMax --value 2>/dev/null || true)"
cur_slice_swap="$(systemctl --user show "$slice" -p MemorySwapMax --value 2>/dev/null || true)"
if [ "$cur_slice_max" != "$slice_max_bytes" ] || [ "$cur_slice_swap" != "$slice_swap_bytes" ]; then
  systemctl --user set-property --runtime "$slice" "MemoryMax=$slice_max_bytes" "MemorySwapMax=$slice_swap_bytes"
fi

if [ "$exec_mode" -eq 1 ]; then
  exec systemd-run --user --scope --unit="$unit" --slice="$slice" --quiet \
    -p "MemoryMax=$mem_max_bytes" \
    -p "MemorySwapMax=$swap_max_bytes" \
    -p "OOMPolicy=kill" \
    -p "MemoryAccounting=yes" \
    -- "$@"
fi

exec 3<&0
child_pid=""
cleanup_on_signal() {
  systemctl --user stop "${unit}.scope" 2>/dev/null || true
  if [ -n "$child_pid" ]; then
    wait "$child_pid" 2>/dev/null || true
  fi
  exit 143
}
trap cleanup_on_signal INT TERM

set +e
systemd-run --user --scope --unit="$unit" --slice="$slice" --quiet \
  -p "MemoryMax=$mem_max_bytes" \
  -p "MemorySwapMax=$swap_max_bytes" \
  -p "OOMPolicy=kill" \
  -p "MemoryAccounting=yes" \
  -- "$@" <&3 3<&- &
child_pid=$!
wait "$child_pid"
rc=$?
set -e
trap - INT TERM
exec 3<&-

if [ "$rc" -ne 0 ]; then
  props="$(systemctl --user show "${unit}.scope" -p Result -p MemoryPeak -p MemorySwapPeak 2>/dev/null || true)"
  systemctl --user reset-failed "${unit}.scope" 2>/dev/null || true
  result="$(printf '%s\n' "$props" | awk -F= '$1 == "Result" {print $2}')"
  if [ "$result" = "oom-kill" ]; then
    peak_raw="$(printf '%s\n' "$props" | awk -F= '$1 == "MemoryPeak" {print $2}')"
    if [[ "$peak_raw" =~ ^[0-9]+$ ]]; then
      peak_str="$(awk -v b="$peak_raw" 'BEGIN {printf "%.1f MiB", b / 1048576.0}')"
    else
      peak_str="${peak_raw:-unknown}"
    fi
    echo "MEM_LIMIT_EXCEEDED: profile=$profile label=$label unit=${unit}.scope slice=$slice limit=$mem_max swap_limit=$swap_max slice_limit=$slice_max peak=$peak_str (cgroup OOM kill)" >&2
    exit 137
  fi
fi

exit "$rc"
