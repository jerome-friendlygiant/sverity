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

Use one defect per bad fixture. Cover safe, boundary and unknown cases. Share a run for identical RTL and options when it preserves the assertions and required fixture shapes. Recheck expected lines after editing headers.

The catalog owns status and gaps. Mark a rule `implemented` only when it covers the full title. Use a `gap` for partial coverage. Mark it `validated` only with a written severity policy and independent review. Keep status and gaps in the catalog.

Give each build its own build directory and each edited file one owner. Check delegated changes yourself. Agents must not commit or push on your behalf.

## Qualify a workflow

Use the [lint workflow](docs/rules/LINT_WORKFLOW.md) as the example for CDC and RDC. Agree on scope and budgets with the maintainer. Define defect classes, freeze blind inputs before scoring, and keep the original scores. Measure against pinned public RTL.

Report what ran, what was skipped, the tool's counts and any decisions the maintainer must make.
