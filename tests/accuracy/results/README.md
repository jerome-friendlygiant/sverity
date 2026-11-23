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

## CDC held-out pilot

The [first CDC freeze](cdc-1-fresh-freeze.json) pins 13 authored cases with two
independent AI readings and a separate settlement where needed. Of 21 candidates,
eight were excluded before scoring because the intended safe controls included
foreign-domain reset crossings. An earlier 24-case staged set was lost from scratch
before it was frozen or scored. It contributes no inputs to this pilot.

The pilot lacks external-boundary inputs and has gaps in safe controls for reset
interaction, reconvergence and qualified protocols. Its first score measures
these inputs only. The [CDC workflow](../../../docs/rules/CDC_WORKFLOW.md) defines
the qualification gate.

The [original first score](cdc-1-fresh-first-score.json.gz) detected all eight bad
cases and disclosed both unknowns, with no execution errors or false errors.
Two of three good controls produced warnings. The pilot passed seven of nine
numeric budgets and failed the false-alarm and good-case review limits.

The two warning sites illustrate why structural controls need complete contracts
for the whole-IP noise budget. `fresh-c09` has separate registered Gray chains,
but supplies no bound on inter-bit arrival time. CDC-009 reports a coherency risk.
`fresh-c20` has a direct two-stage level synchronizer, but no minimum source
hold-time contract. CDC-013 reports that a short level could be missed.
The frozen labels and original outcomes are preserved.

## Second CDC held-out batch

The [second freeze](cdc-2-fresh-freeze.json) preserves 14 independently read
cases from 28 authored candidates. The [pre-score exclusions](cdc-2-fresh-exclusions.json)
record the 14 rejected cases. Reader A saw aggregate development results in the
workflow document and reported excluding them from labelling. Neither reader
accessed analyzer outcomes for these inputs. The retained cases represent five
classes. Multi-bit reconvergence and qualified protocols lack cases in this batch.

The [original first score](cdc-2-fresh-first-score.json.gz) passed six of nine
budgets. One of two bad cases was detected and the other was Unresolved. Eight
of nine good cases were clean, and all three unknowns were disclosed. The batch
exceeded the unresolved-bad, false-alarm and good-case review limits. No case
had a false error or an execution error.

`broader-b12` directly samples an input with an explicitly asynchronous launch
clock. CDC-029 finds the site but leaves it Unresolved. `broader-b16` has a
structural two-stage synchronizer, but its SDC does not specify source hold time
or protocol startup across independent resets. CDC-013 and CDC-041 remain
reviewable. Both original labels and scores are preserved.

## Targeted protocol holdout

The [protocol freeze](cdc-3-protocol-fresh-freeze.json) preserves 11 accepted cases
from 14 authored candidates, with two independent readings and settlement where
needed. The [pre-score exclusions](cdc-3-protocol-fresh-exclusions.json) record the
three rejected cases. Three standalone digital simulations exercised selected
handshake and event schedules during authoring. Those simulations leave physical
timing and behavior under untested schedules open.

The [original first score](cdc-3-protocol-fresh-first-score.json.gz) detected both
bad cases. One good case was clean and five of eight unknowns were disclosed.
The three silent unknowns fail the zero-silent-unknown budget. They exercise
output-only reconvergence, coherence of separately captured output bits, and the
unknown pulse-width contract of a synchronized primary input. The only whole-IP
good control was classed as an IP boundary case, so the batch leaves a gap in
qualified-protocol and reconvergence noise controls.

## Per-IP contract holdout

The [per-IP freeze](cdc-4-ip-fresh-freeze.json) preserves 12 cases from 14
candidates. Two blind readers and a third settler judged the RTL, SDC and
operating contracts before executing SVerity. The
[pre-score exclusions](cdc-4-ip-fresh-exclusions.json) record the two unsuitable
cases. The retained cases cover six of seven workflow classes, with whole-IP
good controls for synchronizer structure and clock/reset interaction.

The [first score](cdc-4-ip-fresh-first-score.json.gz) met 7 of 9 budgets. Of seven
bad cases, four were detected, two were unresolved, and one was missed. Both
good cases were clean and all three unknowns were disclosed. There were no
execution failures. The miss rate was 1/7 against a 5% limit, and the unresolved
bad rate was 2/7 against a 10% limit. The missed case was critical.

C06 can lose a one-source-cycle input event. Its operating contract specifies
the one-cycle event, while the SDC declares clocks and supplies neither the input
launch assignment nor the output observation clock. The first score has a
CST-011 obligation for the missing input delay and no CDC finding at the event
site. C02 lacks an input launch assignment, leaving the direct capture
Unresolved under CDC-029. For C11, RDC-003 reports the unsynchronized reset
release. The CDC-family score instead credits the Unresolved input-data
finding at the shared procedure site.

C05 scores as detected by site matching. Independent simulation confirmed
duplicate delivery when the receiving logic leaves valid asserted after the
first acceptance. The SVerity report retains a CDC-024 functional handshake
obligation and CDC-039 payload uncertainty. It does not diagnose the duplicate
delivery. C09 remains unknown for startup and pointer use. Its simulation covers
15 Gray transitions and does not exercise a full wrap or FIFO operation. This
batch leaves clock inventory, clean protocol and FIFO noise controls, and
physical closure open.

## Contract-complete CDC tranche 1

The [freeze](cdc-5-contract-fresh-freeze.json) preserves 28 of 30 candidates
and the two blind readings plus Opus 5.5 settlement. T01 failed elaboration;
T23's genuine FIFO underflow belongs to functional full/empty closure under
the agreed structural CDC scope. Both [exclusions](cdc-5-contract-fresh-exclusions.json)
were decided before the analyzer ran. T04 is unknown because the contract
does not state consumer timing tolerance for the reconvergent decision.
This does not establish glitch freedom. T07 can earn credit for its separately
identified first-stage tap and leaves its second payload-window defect outside
that detection credit.

The preserved [first score](cdc-5-contract-fresh-first-score.json.gz) met 3 of 9 budgets.
Of 13 bad cases, nine were detected, three were unresolved and one was missed.
Of nine good controls, four were clean under the CDC-family scoring policy,
two were noisy and three were unresolved. Five of six unknowns were disclosed.
There were no execution failures or false errors. T24 was the only structural
whole-IP clean control accepted by the settler. The remaining good cases
supply narrower CDC controls with independently open reset or protocol
premises. Good-case counts here measure CDC noise. They do not certify
whole-IP reset closure.

At that first score, T14 and T30 failed strict attribution because the
clock-control and opaque-source diagnostics lacked structured signal
identifiers. The console messages did describe their issues. T15's explicit
short-event contract was unresolved at its async-reset sampler. Equal-duration,
best-effort and persistent-level controls T20, T21 and T27 hit the same proof
limit. T08 exposed the bounded detector-recognition limit and T13 matched
RDC-003 Unresolved under its predeclared cross-family rule.

The regression replay after independent review met 5 of 9 budgets on the
unchanged freeze. Of 13 bad cases, 11 were detected and two were unresolved.
All six unknowns were disclosed. There were no misses, execution failures,
false errors, or silently accepted critical defects. CDC-045 detected T14
and CDC-020 disclosed its unknown enable relationship. CDC-013 detected
T15, and CDC-023 plus CST-005 disclosed T30.

Of nine good controls, five were clean, two were noisy and two were
unresolved. T21 is clean under best-effort delivery. T20 and T27 remain
unresolved because their required-delivery contracts do not establish
sampling outside reset. A preliminary replay counted both as clean based
on post-reset operation and met 6 of 9 budgets. T17 and T18 still report
CDC-044 and CDC-024/039 respectively. The overages are false-alarm rate
(2/9), unresolved-bad rate (2/13), unresolved-good rate (2/9), and
good-case review rate (4/9). The original score remains the first-score
artifact above.

The hierarchical FIFO and synchronizer-consumer updates keep the same
defect outcomes and disclose all six unknowns. T17 becomes clean because
the scalar delay line has no adjacent-stage consumer or parallel word.
The replay still meets 5 of 9 budgets: six good controls are clean, one
is noisy and two remain unresolved. The frozen inputs and first score
are unchanged.

## Whole-IP CDC control tranche

The [freeze](cdc-6-whole-ip-fresh-freeze.json) preserves 34 cases with two
blind readings and independent settlement. The [pre-score record](cdc-6-whole-ip-fresh-exclusions.json)
excludes Q032 as a duplicate of Q016 and records the schema translation of
Q016's reviewed 2 ns event contract. Its original contract and SDC are
preserved. The attached clock declarations were kept as reviewed.

The [first score](cdc-6-whole-ip-fresh-first-score.json.gz) used the analyzer
built at `fa59967` and met 8 of 9 budgets. Nine of ten bad cases were detected,
all ten unknowns were disclosed, and all fourteen good cases were clean.
The critical miss was Q001. CDC-019 and CDC-031 describe its clock mux, but
neither finding carries the structured signal needed to match the frozen
site. That unresolved hazard report did not earn defect-specific credit.

The attribution replay met all 9 budgets. Q001 became unresolved with
located mux-output evidence. This was the only case outcome that changed.
The two rule fingerprints, severity and confidence stayed the same.

The fourteen whole-IP clean controls vary a no-crossing template. They
measure false alarms on those controls and leave per-class lookalike
precision unmeasured. The pre-score settlement reclassified the cases by
their actual topology, so the accepted batch is not balanced by class.

## CDC RFC 1.0 Wave 3 mutation and reviewed residual artifacts

| Artifact | Revision / binary SHA-256 | What it preserves |
|---|---|---|
| [`cdc-reviewed-residuals-rfc1.json`](cdc-reviewed-residuals-rfc1.json) | `65b12db` (`a5c7d14b52ae6150ce93d25b441202a39724cf574a69ad89470bf6073848fa6d`) | Step 4B reviewed-contract residual tally over `2.894 kLOC` (`46` `cdc_deck` non-INFO findings: `0 FATAL`, `0 ERROR`, `1` confirmed FP = `0.346/kLOC`, `0` not-analysed = `0.000/kLOC`, `39` obligations = `13.476/kLOC`; 5/5 budgets pass), explicit dispositions of all 7 `established`/`heuristic` findings, plus re-verified Probes A, B, and C. |
| [`cdc-mutation-rfc1-wave3-first.json`](cdc-mutation-rfc1-wave3-first.json) | `96a6bfb` (`8c5185b66075c4f925d543ccf69ad6b813428ebabf58f34ea26f1b8b9a653310`) | Preserved blind first score on the frozen 21-candidate RFC §7 Wave 3 set (`20/21 CAUGHT`, `1 MISSED` on `w3-axis-toggle-stage-logic`; frozen operators SHA-256 `5e3d44dac323d583b046bb26d92bf8aafd8b0629a68acd1adf8c5bb734a18ec7`; unnormalized raw file SHA-256 `052c2041f91da67a25564287a5882c2c57972c3ccf268e927677e2415373645f`). |
| [`cdc-mutation-rfc1-wave3-regression.json`](cdc-mutation-rfc1-wave3-regression.json) | `65b12db` (`a5c7d14b52ae6150ce93d25b441202a39724cf574a69ad89470bf6073848fa6d`) | Verified post-fix regression score on the unchanged 21-candidate Wave 3 freeze (`21/21 CAUGHT`, `0 MISSED`, `0 INVALID`; unnormalized raw file SHA-256 `a1aa93f6edbd82eb217b26b77b00e79b47eb1d6ae080448997748b9da9390378`). |

## RFC 1.0 representative CDC holdout (`cdc-7-rfc1-fresh`)

The [freeze manifest](cdc-7-rfc1-fresh-freeze.json) pins 45 blind cases (`rfc1-h01` to
`rfc1-h45`) authored by Claude Sonnet 5.5, read blindly by GPT-6.2 Sol and Gemini 4
Argon (`45/45` agreement on `verdict` and `critical`), and settled pre-freeze by Claude
Opus 5.5 with [zero exclusions](cdc-7-rfc1-fresh-exclusions.json). The batch spans all
seven CDC workflow classes: `19` `bad` (`14` critical), `14` whole-IP `good` controls,
and `12` `unknown` cases.

- **Preserved blind first score ([`cdc-7-rfc1-fresh-first-score.json.gz`](cdc-7-rfc1-fresh-first-score.json.gz)):**
  Scored one-shot on `65b12db` (`--profile asic --goal cdc --deck cdc`, binary SHA-256
  `a5c7d14b52ae6150ce93d25b441202a39724cf574a69ad89470bf6073848fa6d`). Met **7 of 9**
  budgets: `17/19` `bad` detected, `2` critical misses (`rfc1-h04` `CDC-006`, `rfc1-h14`
  `CDC-017`; `missed_rate = 0.105 > 0.05`), `13/14` `good` clean (`1/14` unresolved `0.071`),
  `9/12` `unknown` disclosed, `3` empty-`signals` silent unknowns (`rfc1-h35` `CST-004`,
  `rfc1-h36` `CST-009`, `rfc1-h37` `CDC-043`), `0` false errors, `0` false alarms, and `0`
  execution errors.
- **Verified post-fix regression ([`cdc-7-rfc1-fresh-regression.json.gz`](cdc-7-rfc1-fresh-regression.json.gz)):**
  Re-scored against the unchanged freeze on `c254518` (binary SHA-256
  `77e4cb4198a76b18296b409d6c11ebefd010bbbebd22057658c4abb8e17e63f9`, independently
  approved by Claude Opus 5.5). Meets **9 of 9** budgets: `19/19` `bad` detected
  (`0.0` missed, `0.0` unresolved), `13/14` `good` clean / `1/14` unresolved (`0.071`),
  `12/12` `unknown` disclosed (`0` silent), `0` false errors, `0` false alarms, `0`
  silently accepted critical defects, and `1.689` review findings per case.
