#!/usr/bin/env python3
"""Helpers for running child commands under SVerity's systemd cgroup v2 RAM guard."""
import itertools
import os
import pathlib
import re
import subprocess

MEM_GUARD_SH = pathlib.Path(__file__).resolve().parent / "mem_guard.sh"
MEM_LIMIT_MARKER = "MEM_LIMIT_EXCEEDED:"
_COUNTER = itertools.count(1)


def _sanitize_label(label):
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "_", str(label or "run")).strip("_")
    return (cleaned or "run")[:40]


def make_unit(profile, label=""):
    """Return a unique transient systemd scope name (without the .scope suffix)."""
    safe_profile = _sanitize_label(profile)
    safe_label = _sanitize_label(label)
    return f"sverity-{safe_profile}-{safe_label}-{os.getpid()}-{next(_COUNTER)}"


def build_command(
    cmd,
    *,
    profile="public",
    label="",
    mem_max=None,
    swap_max=None,
    slice_name=None,
    slice_max=None,
    unit=None,
    exec_mode=True,
):
    """Wrap `cmd` in `bash tools/dev/mem_guard.sh ... -- <cmd>` and return `(argv, unit)`.

    Starts with `"bash"` so existing mocks matching `command[0] == "bash"` remain compatible.
    """
    chosen_unit = unit or make_unit(profile, label)
    guard = ["bash", str(MEM_GUARD_SH), "--profile", str(profile), "--unit", chosen_unit]
    if label:
        guard += ["--label", str(label)]
    if mem_max is not None:
        guard += ["--mem-max", str(mem_max)]
    if swap_max is not None:
        guard += ["--swap-max", str(swap_max)]
    if slice_name is not None:
        guard += ["--slice", str(slice_name)]
    if slice_max is not None:
        guard += ["--slice-max", str(slice_max)]
    if exec_mode:
        guard.append("--exec")
    guard.append("--")
    guard.extend(str(part) for part in cmd)
    return guard, chosen_unit


def stop_unit(unit):
    """Best-effort stop of a transient scope after a subprocess timeout."""
    if not unit:
        return
    scope = unit if str(unit).endswith(".scope") else f"{unit}.scope"
    try:
        subprocess.run(["systemctl", "--user", "stop", scope], capture_output=True, text=True, timeout=5)
        subprocess.run(["systemctl", "--user", "reset-failed", scope], capture_output=True, text=True, timeout=5)
    except OSError:
        pass
    except subprocess.SubprocessError:
        pass


def inspect_oom(unit, stderr="", returncode=0, *, profile="", label="", mem_max=None):
    """Return `(is_oom, detail)` for a completed guarded subprocess."""
    for line in (stderr or "").splitlines():
        if MEM_LIMIT_MARKER in line:
            return True, line.strip()
    if returncode == 0 or not unit:
        return False, ""
    scope = unit if str(unit).endswith(".scope") else f"{unit}.scope"
    try:
        show = subprocess.run(
            ["systemctl", "--user", "show", scope, "-p", "Result", "-p", "MemoryPeak", "-p", "MemorySwapPeak"],
            capture_output=True, text=True, timeout=5,
        )
        subprocess.run(["systemctl", "--user", "reset-failed", scope], capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return False, ""
    if show.returncode != 0:
        return False, ""
    props = {}
    for line in show.stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            props[key] = value.strip()
    if props.get("Result") != "oom-kill":
        return False, ""
    peak_raw = props.get("MemoryPeak", "")
    if peak_raw.isdigit():
        peak_str = f"{int(peak_raw) / 1048576.0:.1f} MiB"
    else:
        peak_str = peak_raw or "unknown"
    detail = (
        f"{MEM_LIMIT_MARKER} profile={profile or 'unknown'} label={label or scope} "
        f"unit={scope} limit={mem_max or 'default'} peak={peak_str} (cgroup OOM kill)"
    )
    return True, detail


def available_gib():
    """Return host `MemAvailable` in GiB from `/proc/meminfo`, or `None` if unavailable."""
    try:
        for line in pathlib.Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) / 1024 / 1024
    except (OSError, ValueError, IndexError):
        return None
    return None
