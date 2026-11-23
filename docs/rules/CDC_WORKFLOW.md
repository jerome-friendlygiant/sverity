# CDC workflow

[Scope and gate](#scope-and-gate) explains what the CDC gate checks and what is excluded.
The [defect classes](#defect-classes-and-closure) define what the CDC deck covers. To
change that coverage or tune a rule, follow [Changing checks](#changing-checks), then use
[Verification](#verification) and [Qualification](#qualification) to decide what evidence
to collect. [LINT_WORKFLOW.md](LINT_WORKFLOW.md) is the model for this page.

**SVerity has not been compared against any commercial CDC tool on any design.** A family
replaces a commercial tool only after passing its own [phase gate](../ROADMAP.md#2-mid-term-reaching-10).

## Scope and gate

**W-CDC-1** is per-IP structural CDC for synthesizable ASIC SystemVerilog. Run each supported
configuration with its elaborated top, complete source list, defines, parameters, reviewed
clock intent and boundary contracts. Record the configuration and any unexamined modes. Use
the [supported constraint subset](ANALYSIS.md#what-the-sdc-reader-applies) and supply
reviewed models for library cells.

```sh
sverity --profile asic --goal cdc --deck cdc --exit-policy strict ...
```

`--deck cdc` is the frozen deck of 60 rules: 38 CDC, 15 CST and 7 coupled RDC. The
[catalog](catalog.json) owns each rule's status and gaps, and the [CDC rule
specification](CDC.md) owns the predicates.

| Result | Disposition |
|---|---|
| Execution failure or unsupported scope that can affect a claimed crossing | Blocks acceptance for that configuration |
| Unwaived ERROR or FATAL | Blocks acceptance |
| WARNING, Unresolved or a proof obligation | Requires a documented disposition with an owner and evidence |
| INFO | Review the inventory and recognition assumptions alongside the blocking findings |

Record execution, coverage and review independently. Preserve the full JSON report. A run
with INCOMPLETE coverage can still measure capacity, but it leaves CDC review open. Inspect
every coverage entry and analysis limit and reconcile it with the published CDC claims.
False and multicycle paths do not establish CDC safety. A waiver records the review decision
and keeps the finding and its coverage state.

**Frozen scope decisions** (maintainer, 2026-11-21):

- Retain 38 CDC rules and the 15 CST rules `CST-001` to `006`, `009` to `014`, `018`, `019`
  and `022`. Retain the seven RDC rules the deck needs: `RDC-002`, `003`, `006`, `007`,
  `016`, `023` and `024`.
- Exclude before the freeze, with written reasons: `CDC-012` (kept as preview), `CST-007`,
  `008`, `015`, `016`, `017`, `020`, `021`, `023` and `024`. The other 18 active RDC rules
  belong to Phase 4 RDC. `CST-017` and `CST-020` wait for Phase 3 and `CST-016` for Phase 4.
- The FPGA profile is outside this scope. Use `--profile asic` with reviewed per-design
  `--library` models.
- Residual noise budgets on the five pinned CDC designs under reviewed contracts: 0 FATAL, 0
  ERROR, at most 1 confirmed false positive, 2 not-analysed findings and 15 obligations per
  analysed kLOC.
- A held-out batch passes when its verified regression meets all nine
  [CDC budgets](../../tests/accuracy/cdc-budgets.json). The first score is preserved beside it.
- The independent team switching trial is deferred for 1.0. It is required for Phase 3.

**Primary-input events.** Put reviewed `boundary_events` entries in the JSON config. Bind each
exact input bit to an SDC launch clock and state the protocol kind, a finite minimum permitted
assertion duration with units, and the delivery requirement. The report records the contract
and its validation. Do not use interface names, generic clock periods or case prose instead.
An unresolved or unapplied contract blocks any claim about pulse-loss behavior. The evidence
covers ideal internal sampling only.

**Out of scope.** VHDL and mixed language, gate-level netlists, UPF/CPF, formal proof,
reusable hierarchical block summaries, automatic mode sweeps, SGDC import, and STA and MTBF
sign-off. Timing, physical and functional obligations that structure cannot show stay open
and are listed in the defect-class table below.

## Defect classes and closure

| Class | Detection or disclosure required | Closure evidence |
|---|---|---|
| Clock intent and inventory | Missing roots, conflicting relationships and unapplied mode declarations | Reconcile the RTL clock inventory with reviewed supported constraints and generated-clock assumptions |
| Synchronizer structure | Missing or short chains, stage logic, early fan-out and inconsistent clock or reset | Review source and destination registers and intended chain use. Check implementation attributes and physical metastability requirements externally. |
| Multi-bit transfer and reconvergence | Unsafe independent capture, unchecked correlation and combinational influences | Review coherence at the consumer and verify any timing or protocol assumption used to justify the crossing |
| Qualified payload and event protocols | Unqualified capture, malformed toggle or handshake, unproven stability and event rate | Verify payload stability around capture, event delivery and acknowledgment with reviewed assertions or equivalent design evidence |
| Gray pointer FIFO and cross-clock memory | Pointer structure, incomplete synchronization, missing reset and unresolved memory behavior | Verify pointer transitions, reset startup, full and empty behavior, collision policy and physical bit skew |
| Clock and reset interaction | Unrecognized clock gates and muxes, unsafe enables and reset-dependent transfer activation | Review clock control and mode transitions. Verify reset release and recovery before the protocol can transfer. |
| IP and external boundaries | Unmodeled launch and capture domains, opaque cells and unknown input or output contracts | Document the source, sink and reset assumptions for the relevant ports and reconcile with the integrating design |

A silent seeded functional defect fails qualification. Before authoring a blind batch, record
each class's claim and the expected behavior for bad, safe and unknown cases. The catalog maps
rules to these classes and `tools/dev/conformance.py` shows the coverage.

## Changing checks

Follow the six steps in [AGENTS.md](../../AGENTS.md) and [LINT_WORKFLOW.md](LINT_WORKFLOW.md#changing-checks).
For CDC in particular:

1. **Reproduce and name the class.** Keep the minimal input as a fixture. Look for an existing
   predicate in [CDC.md](CDC.md), [CST.md](CST.md) or [RDC.md](RDC.md) first.
2. **Fix the shared model first.** Clock roots, relationships, flow and cones serve many rules.
   Record changed assumptions in [ANALYSIS.md](ANALYSIS.md).
3. **One predicate per rule.** State trigger, exemptions, severity and the unknown outcome.
   Report Unresolved when the analysis cannot decide. Mark an unsupported scope INCOMPLETE.
4. **Test lookalikes before widening an exemption.** Every exemption needs a safe and an unsafe
   lookalike. A reviewer replays the exemption with adversarial inputs, such as a bypass,
   a mismatched edge or reset, an unknown relationship and an opaque call.
5. **Update the catalog, spec and fixtures together.** A rule is `validated` only with a written
   severity policy, all five fixture shapes (good, bad, boundary, unknown and mutation) and an
   independent review.
6. **Verify and review.** Run `tools/dev/check.sh`. For a substantial change, an independent
   reviewer runs adversarial inputs against the binary. Keep a small fixture for each
   confirmed miss.

## Severity policy

| Severity | Use |
|---|---|
| FATAL | The run cannot produce a trustworthy result. |
| ERROR | Analysis establishes a structural CDC defect, such as an unsynchronized asynchronous crossing, inter-stage logic, a broken Gray pointer or a raw payload capture. |
| WARNING | Potential CDC defect, unproved protocol or timing obligation, or Unresolved analysis. |
| INFO | Inventory disclosure or a recognized synchronizer or contract structure with stated assumptions. |

An Unresolved finding or unproved obligation stays at least WARNING. The same contract applies
to [lint](LINT_WORKFLOW.md#severity-policy).

## Verification

Run `tools/dev/check.sh` for every change. It builds, runs CTest and checks the pinned public
RTL. Run standalone tools below under `tools/dev/mem_guard.sh` and in a scratch directory
outside the repository.

### Accuracy batch

Freeze, score once and keep the first score, as in the
[lint procedure](LINT_WORKFLOW.md#freeze-and-score-a-new-batch). Pass the CDC budgets:

```sh
python3 tools/dev/accuracy.py --batch BATCH --strict-fresh-labels --freeze-batch freeze.json
python3 tools/dev/accuracy.py build/sverity --batch BATCH --strict-fresh-labels \
  --verify-freeze freeze.json --first-score --budgets tests/accuracy/cdc-budgets.json \
  --check --json first-score.json
```

The [budgets](../../tests/accuracy/cdc-budgets.json) allow up to 5% missed bad cases and 10%
each for unresolved bad cases, false alarms on good cases, unresolved good cases, good cases
needing review and good cases with not-analysed findings. They allow zero false errors, zero
silently accepted critical cases, zero silent unknown cases and zero critical defects found
only by non-gating findings. Judge each critical CDC class separately, because the scorer's
class-completeness metric is lint-specific.

Label rules for CDC cases:

- Agree `expected_rules` and final site signals before freezing. A finding must match an
  expected rule and a signal. Use a qualified name where instances reuse a name. Bit indices
  must agree when both name one. A whole-vector finding can match a labelled bit.
- An RDC or CST finding counts when the readers name its rule. A nearby line alone does not.
- CDC-024 and CDC-039 may disclose unknowns. Waived, resolved and not-analysed findings and
  protocol obligations earn no detection credit on bad cases.
- A bad label needs at least one rule able to detect the defect. Keep an in-scope defect with
  no detector in the denominator: freeze it as bad with empty `expected_rules` and a
  `detection_gap` reason agreed before scoring. Out-of-scope cases and invalid RTL need a
  documented exclusion before the freeze.
- For `boundary_events`, set `boundary_event_config` in the label to a case-relative JSON file.
  The manifest pins its bytes. Keep SDC paths in the label.

The [artifact index](../../tests/accuracy/results/README.md) links each score to its inputs.

### Known-good corpus and reviewed residuals

`tools/dev/cdc_reviewed_residuals.py` runs the five pinned public designs in
[the manifest](../../tests/public_projects/manifest.json) under their
[reviewed SDCs](../../tests/public_projects/reviewed_sdc/) and counts non-INFO findings of
deck rules per analysed kLOC. It checks FATAL, ERROR, not-analysed and obligation budgets. It
reports established and heuristic findings as the unreviewed false-positive bound, and each
needs a written disposition against the RTL.

```sh
python3 tools/dev/cdc_reviewed_residuals.py build/sverity SCRATCH --json residuals.json
```

The structural run without reviewed SDCs must also show 0 FATAL and 0 ERROR. Review every
ERROR on a known-good design as a possible false ERROR. The three recorded probes are:
`axis_async_fifo` with `FRAME_FIFO=1`, its equivalent binary-pointer mutant at `FRAME_FIFO=0`,
and `cc_cdc_fifo_2phase.dst_data_o` with an output delay. The first two give 0 ERROR. The third
keeps `CST-012` Unresolved on purpose, because per-bit qualification of the output data arm is
outside the 1.0 model.

### Mutation tranche

`tools/dev/cdc_mutation_score.py` changes one site per mutant in the pinned public RTL or in a
scratch copy of the reviewed SDC, and pairs each mutant with its unmutated baseline. A mutant
counts as caught only when its finding is new, names the required rule and endpoints, and is an
established or heuristic WARNING, ERROR or FATAL. Nearby, pre-existing, unresolved,
obligation-only and waived findings earn nothing. The runner fails closed when a target is
missing or ambiguous.

```sh
python3 tools/dev/cdc_mutation_score.py build/sverity SCRATCH --json mutation.json \
  --operators OPERATORS.json --profile asic --deck cdc \
  --sdc-dir tests/public_projects/reviewed_sdc
```

Without `--operators`, the runner replays the four built-in wave 1 mutants. Freeze the operator
list before the first score, review each candidate blind for equivalence, lint the mutated RTL
with Verilator, and keep the first score beside later regressions. Report invalid and
equivalent mutants separately. The Wave 3 operator set is identified by its SHA-256 in
the result files.

Written exclusions, each with paired `// revert:` fixtures: a derived-clock root (`CDC-002`),
individually synchronized bus bits (`CDC-009`), `CDC-003` after dropping clock groups,
synchronizer stage edge or reset mismatch (`CDC-026`), and a too-short or asserted-constant
reset synchronizer (`RDC-003`, `RDC-006`). The five public designs have no mutable site for
these, and the fixtures cover them.

### Clock inventory

`tools/dev/clock_inventory.py` writes a deterministic inventory of the clock roots in the
elaborated model of the pinned OpenTitan `top_earlgrey` and the active SDC clock name of each.
It runs no rules and claims no CDC result. Use it to check proposed SDC bindings and keep the
unmatched roots for review before a full run.

```sh
cmake --build BUILD --target sverity-clock-inventory
python3 tools/dev/clock_inventory.py BUILD/sverity-clock-inventory SCRATCH \
  --top top_earlgrey --sdc REVIEWED.sdc --max-gb 6 --metadata run.json > inventory.json
```

The runner checks memory and disk first, caps address space and time, and records the
OpenTitan pin, file list and SDC hashes, host and limits in the metadata sidecar. An
unmapped declaration matched no modeled root. Several active names on one root are reported
as ambiguous. The inventory does not establish relationships, modes, timing or physical
behavior. Different clock rates alone do not make clocks asynchronous. Leave unreviewed
relationships unknown, and add no groups, case values or exceptions without reviewed intent.

The pinned OpenTitan `clocks.xdc` targets chip wrappers and uses commands outside the SDC
subset, so it cannot be copied as the SoC-top constraint.

### Capacity

Measure whole `top_earlgrey` with the exact top, files, constraints, toolchain and host. Use a
separate scratch directory and the reviewed constraints:

```sh
python3 tools/dev/capacity.py build/sverity SCRATCH --top top_earlgrey \
  --profile asic --goal cdc --deck cdc \
  --sdc tests/public_projects/reviewed_sdc/top_earlgrey.sdc \
  --library tests/public_projects/reviewed_sdc/top_earlgrey.cells.json \
  --exit-policy strict
```

The runner caps address space and checks host memory before it starts. Record the JSON row,
input and report hashes, build toolchain, elapsed time and peak resident memory. Run it twice
and compare the report hashes. Read analyzer exit, coverage and review status separately: a
completed run with incomplete coverage measures capacity and leaves CDC review open.

## Qualification

### Gate status

| Gate | Status |
|---|---|
| Deck of 60 rules | 60 of 60 `validated` with all fixture shapes and a written severity (`python3 tools/dev/conformance.py`) |
| Honest `COMPLETE` and strict exit 0 | A clean constrained fixture exits 0. The five adverse cases below are INCOMPLETE or flagged (`cli_cdc_deck_clean_complete_vs_five_adverse_cases`). |
| Known-good corpus | 0 FATAL and 0 ERROR on the five pinned designs under structural and reviewed SDCs |
| Mutation recall and residuals | Met (results below) |
| Held-out accuracy | Met in verified regression (results below) |
| Whole-SoC capacity | Completed, accepted over its original budget (results below) |
| Release package and disclosures | Open |
| Phase 3 commercial replacement | Open |

The five adverse cases: two asynchronous SDC names on one RTL clock root (`CST-003`); a
generated-clock declaration that disagrees with its RTL source (`CST-013`); `CDC-038`
exceptions with `-through` nets, which the rule does not walk; netlist-style selectors with no
RTL match (`CST-002`); and a transfer through subroutine-local static state (`CST-010`).

### Results

| Requirement | Result | Evidence |
|---|---|---|
| Rules independently reviewed | All 60 deck rules passed adversarial binary review by independent AI reviewers (Claude Opus 5.5, GPT-6.2 Sol, Gemini 4 Argon) with unsafe and safe lookalikes. Reviewer, date and scope are in each catalog entry. | [catalog](catalog.json) |
| Blind mutation recall | Wave 3, 21 candidates across every [RFC section 7](../SVerity_RFC.md#7-testing) mutation class: first score 20 of 21, verified regression 21 of 21. Waves 1 and 2 caught all 8 live public mutants. | [first](../../tests/accuracy/results/cdc-mutation-rfc1-wave3-first.json), [regression](../../tests/accuracy/results/cdc-mutation-rfc1-wave3-regression.json), [wave 2](../../tests/accuracy/results/cdc-mutation-rfc1-wave2.json), [wave 1](../../tests/accuracy/results/cdc-mutation-rfc1.json) |
| Reviewed residuals | 5 of 5 budgets on 2.894 kLOC: 0 FATAL, 0 ERROR, 1 confirmed false positive (0.346 per kLOC), 0 not-analysed, 39 obligations (13.476 per kLOC). Of 7 established or heuristic findings, 6 are real review items. | [residuals](../../tests/accuracy/results/cdc-reviewed-residuals-rfc1.json), [residual review](../../tests/accuracy/results/cdc-residual-review-rfc1.json.gz) |
| Held-out accuracy `cdc-7-rfc1-fresh` | 45 blind cases (19 bad, 14 good, 12 unknown) across all seven classes. First score 7 of 9 budgets: two critical misses and three silent unknowns, since repaired. Verified regression 9 of 9: 19 of 19 bad detected, 13 of 14 good clean, 12 of 12 unknown disclosed, 0 false errors, 0 silently accepted critical defects. | [freeze](../../tests/accuracy/results/cdc-7-rfc1-fresh-freeze.json), [first score](../../tests/accuracy/results/cdc-7-rfc1-fresh-first-score.json.gz), [regression](../../tests/accuracy/results/cdc-7-rfc1-fresh-regression.json.gz) |
| Earlier whole-IP holdout `cdc-6-whole-ip-fresh` | 9 of 9 budgets in regression | [first score](../../tests/accuracy/results/cdc-6-whole-ip-fresh-first-score.json.gz) |

The [accuracy index](../../tests/accuracy/results/README.md) lists every CDC batch. The five
contract-tranche cases `T08`, `T13`, `T18`, `T20` and `T27` of `cdc-5-contract-fresh` remain
documented spec boundaries, not fixed gaps: bounded `CDC-014` detector recognition, external
`RDC-003` recovery and removal, open `CDC-024` and `CDC-039` protocol and stability
obligations, and `CDC-013` post-reset sampler availability.

### Whole-SoC capacity

Measured 2026-11-23 at SVerity `4c883e7`:

| Item | Value |
|---|---|
| Design | OpenTitan `top_earlgrey` at `92f3d03b0dcf6d5c1933a65d1497747f67d19f4c`, FuseSoC 2.4.5 file list of 733 files |
| Clock intent | [`top_earlgrey.sdc`](../../tests/public_projects/reviewed_sdc/top_earlgrey.sdc) and [`top_earlgrey.cells.json`](../../tests/public_projects/reviewed_sdc/top_earlgrey.cells.json) |
| Command | The [capacity](#capacity) command above |
| Run | Completed in 730 s, 11.84 GiB peak RSS. Execution SUCCESS, coverage INCOMPLETE, review FAIL, strict exit 5. |
| Size | 3,287,139 nodes, 15,619,692 edges, 237,159 register bits, 38 clock domains |
| Findings | 0 FATAL, 11,407 ERROR, 164,282 WARNING, 6,763 INFO. The JSON report is 2.4 GB. |
| Reproducibility | Two runs gave byte-identical reports, SHA-256 `516af56b9d4d8dd3588e483e6cd0e65ab6e0db6e74283942637cc25418d1f080` |
| Host | AMD Ryzen 7 PRO 6850H, 16 threads, 22 GiB RAM, Fedora 44 (Linux 7.2), GCC 16.2.1, slang 12.1.0, under a 15 GiB cgroup cap |

INCOMPLETE scopes: clock and reset extraction of logic-generated clocks, unknown external AST
clock relationships, unconstrained inputs and outputs, graph precision from user functions and
wide symbols, the `CDC-015` work limit, and payload qualification.

The target was 600 s and 8 GiB. The maintainer accepted 730 s and 11.9 GiB for 1.0 on
2026-11-23. The crossing stage takes about 555 s, of which reconvergence is about 158 s. The model uses
about 5.4 GiB and the findings about 3 GB. A faster and smaller run stays a
[roadmap](../ROADMAP.md#2-mid-term-reaching-10) item. **The findings have not been triaged
against the RTL.** The 11,407 ERRORs are unreviewed and are not a defect count.

### Still open

For the 1.0 release, and not specific to CDC: reference crosswalks, governance and provenance,
the published frozen deck list with its severity and waiver matrix, and the mandatory
disclosures. These are that independent AI review is not human sign-off, that no commercial
comparison was made, and the out-of-scope list above. The
[RFC acceptance criteria](../SVerity_RFC.md#8-acceptance-criteria-for-10) govern the release
claim.

For Phase 3 commercial replacement, beyond 1.0: `CST-020` complete crossing inventory under
reviewed clock intent, a named closure step for every obligation, mode and boundary closure
(`CST-003`, `CST-012`, `CST-013`, `CST-014`, `CST-017`), and an independent adopter pilot with
measured closure effort. A side-by-side comparison, if licensed access exists, runs both tools
on the same inputs and equivalent clock and boundary intent, reconciles crossings by endpoint,
domain, mode and class, and reads the RTL for each disagreement. Keep proprietary RTL and
vendor reports outside Git. Structural recognition alone leaves the functional, timing and
physical obligations open.
