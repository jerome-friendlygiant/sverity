# Verification

Run `tools/dev/check.sh` from the checkout. It builds the executable, runs CTest
and runs the pinned public-RTL gate, stopping at the first failure.

| Layer | What it verifies |
|---|---|
| `tests/unit/` and Python tests registered in CTest | Analysis invariants, exit and report contracts, and migration and evidence tooling. |
| `tests/fixtures/` | Each rule's expected findings, safe lookalikes, boundaries, unknown cases and mutations. Conformance checks the shapes required by the catalog claim. |
| `tests/public_projects/` | The executable runs on pinned public RTL and satisfies each design's report and resource expectations. |
| `tests/accuracy/` | Labelled cases measure missed defects, noise and disclosure. CTest validates labels and tests scorer behavior. Score batches explicitly with `tools/dev/accuracy.py`. |

## Before changing a rule

1. Read its spec and the shared analysis assumptions. Reproduce a defect on a
   minimal case and keep a fixture for the behavior the fix protects.
2. Use the existing fixtures as regression guards. Check for a safe lookalike
   and an unknown case before broadening an exemption.
3. Run the full gate. For rule sign-off, obtain independent adversarial review and
   record it in the catalog.

## Before making a workflow claim

- **Accuracy.** Keep independent blind readings and the original score for new
  cases. Disclose execution failures and undecided cases. Later scores on cases
  used to drive fixes measure regression.
- **Scale and review effort.** Record the pinned configuration, coverage, time,
  memory and residual findings. Workflow-level measurements run separately from
  the repository gate.
- **Adoption.** An adopter trial measures team review effort on its own IP. The
  1.0 lint scope defers this trial and human label review.

For the concrete lint steps and evidence, see the [lint workflow](rules/LINT_WORKFLOW.md#verification).
