# Lint measurement artifacts

See [lint qualification](../../../docs/rules/LINT_WORKFLOW.md#qualification) for the current interpretation and [freeze and score](../../../docs/rules/LINT_WORKFLOW.md#freeze-and-score-a-new-batch) for collecting a new batch.

| Artifacts | What they preserve |
|---|---|
| `p2-lint-heldout.json` and `p{3,4}-lint-fresh-first-score.json.gz` | Original first scores, including misses and execution failures. Subsequent scores on these inputs measure regression. |
| `p{3,4}-lint-fresh-freeze.json` | The exact membership and bytes supplied for each first score. Verify against the corresponding historical input snapshot. |
| `p{3,4}-lint-fresh-adjudication.json` | Post-score label decisions and the evidence hashes checked against the live labels. |
| `reference-lint-6623e2c.json` and `lint-ledger-6623e2c.json` | The replay counts and reviewed blocking and not-analysed sites at the same revision. The ledger distinguishes designs read directly from sites classified by shared structure. |
| `reference-lint-e6add85.json` and `noise-gate-6623e2c.json` | The follow-up replay and the per-IP ceilings used by the noise gate. |
| `verilator-diff-6623e2c.json` | The site-by-site comparison with Verilator 5.046. OpenTitan requires 5.048. Of 52 sites without a SVerity finding, seven were judged real misses and 45 Verilator false positives. Of the 30 UNOPTFLAT sites, four were traced directly and 26 judged by shared structure. The file also records the SVerity-only direction. |
| `mutation-recall-f5d0816.json` | Seeded sample, per-mutant outcomes and equivalent-mutant disposition. |
| `lint-resource-8660489.json` | Whole-SoC lint and ten-IP runtime and peak memory, including the report comparison under two memory caps. |
