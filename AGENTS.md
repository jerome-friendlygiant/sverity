# AGENTS.md

Use [README.md](README.md) and [Architecture](docs/ARCHITECTURE.md) for build and layout conventions. Read [Analysis](docs/rules/ANALYSIS.md) before changing an analysis.

## Repository rules

- Commit only public material. Keep proprietary RTL, vendor-tool output, credentials, local paths and personal data out of Git. Reference external designs by pinned URL and commit. Record copied code provenance in the family crosswalk.
- Run `tools/dev/check.sh` before each commit. It builds, runs CTest and checks pinned public RTL. Commit small changes. Push only the assigned branch. Open a PR only when asked. Omit co-author trailers and session links.

## Change a rule

1. Reproduce the defect with a minimal input. Check the result against the RTL. Keep the input as a fixture.
2. Fix the shared analysis when several rules have the same cause. Use existing fixtures as regression guards. Change an expectation only when the RTL supports the new result.
3. Use one predicate per rule. State its trigger, exemptions, severity and unknown outcome in the spec. Update the code, spec, catalog and fixtures together.
4. Report Unresolved when the analysis cannot decide. Mark an unsupported scope INCOMPLETE. Omit a finding only when the check proves the defect is absent.
5. Test safe and unsafe lookalikes before widening an exemption. Explain each finding that disappears. Fix missed defects before false alarms. Record proof assumptions in [Analysis](docs/rules/ANALYSIS.md).
6. For substantial changes, have an independent reviewer run adversarial inputs against the binary. Fix confirmed misses and keep a small fixture for each. Reserve the strongest model for review and judgment.

Add a fixture only when it covers a required shape or a branch no other test reaches; a near-copy of an existing case adds run time, not coverage. Use one defect per bad fixture. Cover safe, boundary and unknown cases. Share a run for identical RTL and options when it preserves the assertions and required fixture shapes. Recheck expected lines after editing headers.

The catalog owns status and gaps. Mark a rule `implemented` only when it covers the full title. Use a `gap` for partial coverage. Mark it `validated` only with a written severity policy and independent review. Keep status and gaps in the catalog.

Give each build its own build directory and each edited file one owner. Check delegated changes yourself. Agents must not commit or push on your behalf.

## Build and test memory safety

- Use `tools/dev/check.sh` for verification; it runs a pre-flight `MemAvailable` check (`< 2 GiB` stops immediately), defaults builds to `-j2` (`-j1` when `< 6 GiB` is available) and CTest to `-j2` (`-j1` when `< 4 GiB` is available), and wraps configure, build, and test execution in kernel-enforced memory caps via [tools/dev/mem_guard.sh](tools/dev/mem_guard.sh).
- Never invoke raw `cmake --build ... -j` or `ctest -j` without concurrency limits on a shared host: Release C++20 compilation of heavy translation units (such as `model.cpp` and `lint_model.cpp`) peaks around `1.3–1.8 GiB` RSS per job (`~3.0 GiB` at `-j2`), so `-j4` or `-j16` builds can exhaust host RAM and swap within seconds and trigger system-wide OOM kills.
- CTest registers every fixture runner and test inside `tools/dev/mem_guard.sh` (`SVERITY_CTEST_MEM_GUARD=ON` by default) and serializes heavy targets plus two fixture slots with `RESOURCE_LOCK` (`sverity_heavy`, `sverity_fixture_slot_0`, `sverity_fixture_slot_1`) so even concurrent CTest jobs cannot overload memory. Standalone build, scorer, or binary runs outside `check.sh` / `ctest` should also run under `tools/dev/mem_guard.sh --profile <build|configure|fixture|unit|test|public|robustness|accuracy|capacity> -- <cmd>`.
- Enforced defaults use per-invocation `MemoryMax` + `MemorySwapMax=0` + `OOMPolicy=kill` in a transient systemd user scope under a shared aggregate slice (`SVERITY_MEM_SLICE=sverity.slice`, default `SVERITY_SLICE_MEM_MAX=5G` across concurrent worktrees) instead of relying on RSS polling alone.
- The whole-SoC `top_earlgrey` CDC run peaks near 12 GiB. Run it alone through `tools/dev/capacity.py` with `SVERITY_SLICE_MEM_MAX` raised to fit, never beside a build or CTest.
- Exceeding a memory limit exits `137` and reports `MEM_LIMIT_EXCEEDED: ... (cgroup OOM kill)` distinctly (or `MEMORY LIMIT` in Python runners). Adjust `SVERITY_<PROFILE>_MEM_MAX` or `SVERITY_SLICE_MEM_MAX` only from bounded workload measurements, never via uncapped OOM stress runs.

## Qualify a workflow

Use the [lint](docs/rules/LINT_WORKFLOW.md) and [CDC](docs/rules/CDC_WORKFLOW.md) workflows as the examples for RDC. Agree on scope and budgets with the maintainer. Define defect classes, freeze blind inputs before scoring, and keep the original scores. Measure against pinned public RTL.

Report what ran, what was skipped, the tool's counts and any decisions the maintainer must make.
