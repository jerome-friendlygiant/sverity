# RDC workflow

[Scope and gate](#scope-and-gate) explains what the RDC gate checks and what is excluded.
The [defect classes](#defect-classes-and-closure) define what the RDC deck covers. To
change that coverage or tune a rule, follow [Changing checks](#changing-checks), then use
[Verification](#verification) and [Qualification](#qualification) to decide what evidence
to collect. [CDC_WORKFLOW.md](CDC_WORKFLOW.md) is the model for this page.

**SVerity has not been compared against any commercial RDC tool on any design.** A family
replaces a commercial tool only after passing its own [phase gate](../ROADMAP.md#2-mid-term-reaching-10).

## Scope and gate

**W-RDC-1** is per-IP structural RDC for synthesizable ASIC SystemVerilog. Run each supported
configuration with its elaborated top, complete source list, defines, parameters, reviewed
clock intent, boundary contracts and reviewed reset intent. Record the configuration and any
unexamined modes. Use the [supported constraint subset](ANALYSIS.md#what-the-sdc-reader-applies)
and supply reviewed models for library cells.

```sh
sverity --profile asic --goal rdc --deck rdc --exit-policy strict ...
```

`--deck rdc` is the frozen deck of 40 rules: 25 RDC and 15 CST. The RDC rules are `RDC-001`
to `030`, excluding the retired IDs `RDC-015`, `018`, `021` and `022` and the unimplemented
`RDC-029`. The CST rules are `CST-001` to `006`, `009`, `010`, `011`, `013`, `014`, `016`,
`018`, `019` and `022`. The [catalog](catalog.json) owns each rule's status and gaps, and the
[RDC rule specification](RDC.md) owns the predicates.

| Result | Disposition |
|---|---|
| Execution failure or unsupported scope that can affect a claimed reset crossing | Blocks acceptance for that configuration |
| Unwaived ERROR or FATAL | Blocks acceptance |
| WARNING, Unresolved or a proof obligation | Requires a documented disposition with an owner and evidence |
| INFO | Review the inventory and recognition assumptions alongside the blocking findings |

Record execution, coverage and review independently. Preserve the full JSON report. A run
with INCOMPLETE coverage can still measure capacity, but it leaves RDC review open. Inspect
every coverage entry and analysis limit and reconcile it with the published RDC claims.
A false or multicycle path does not establish reset safety, and RDC-027 says so. A waiver
records the review decision and keeps the finding and its coverage state.

**Frozen scope decisions** (maintainer, 2026-11-23):

- Retain 25 RDC rules and the 15 CST rules listed above.
- Exclude before the freeze, with written reasons:
  - `RDC-029`: needs UPF or CPF power intent, which is unsupported. `RDC-012` discloses the
    missing model or the declared one (see Power intent below).
  - `CST-012`: CDC output contracts. It belongs to the [CDC deck](CDC_WORKFLOW.md).
  - `CST-007`, `008`, `015`, `017`, `020`, `021`, `023` and `024`: the same reasons as the CDC
    deck. They are not implemented and are deferred to commercial replacement.
- The FPGA profile is outside this scope, as for CDC. Use `--profile asic` with reviewed
  per-design `--library` models.

**Reset intent.** Put a reviewed `reset_contracts` section in the JSON config. Sources are
named by exact top input bit or by traced root. The section can declare `assert_together`
groups, `release_order` (before or after, clock, edge and `min_cycles`) and initial `state`.
CST-016 reports each declaration. External declarations are reported as Assumed, never as
proven. A release order is checked structurally only against a recognised reset-synchroniser
chain released under the earlier reset. Do not infer reset intent from interface names, case
prose or generic reset polarity.

A reset contract never changes a finding's severity. A bound contract is cited as evidence and
closes a finding only through a reviewed waiver.

**Power intent.** UPF and CPF are not read. A reviewed config section
`"power_intent": {"model": "single_always_on"}` declares one always-on power domain with no
retention, isolation or power switching. `RDC-012` is then INFO with a trust model and reset
analysis is not marked INCOMPLETE for it. Without the declaration `RDC-012` is WARNING Unresolved
and `reset-extraction` is INCOMPLETE. Any other model is a configuration error.

**RDC-013 closure.** A reviewed waiver narrowed to the reset root (rule and signal), citing the
STA or sign-off evidence for recovery, removal and pulse width. No declaration changes the severity.

**Primary-input events and boundary contracts.** Use the same reviewed `boundary_events`
entries as CDC. Bind each input bit to its SDC launch clock and state the protocol kind, the
minimum assertion duration and the delivery requirement. An unresolved or unapplied contract
blocks any claim about reset pulse behavior. The evidence covers ideal internal sampling only.

**Out of scope.** VHDL and mixed language, gate-level netlists, UPF and CPF power intent,
formal proof, reusable hierarchical block summaries, automatic mode sweeps, SGDC import, and
STA, recovery and removal, and MTBF sign-off. Timing, physical and functional obligations that
structure cannot show stay open and are listed in the defect-class table below.

## Defect classes and closure

| Class | Rules | Closure evidence |
|---|---|---|
| Reset inventory and unknown sources | RDC-001, 002, 012, 013 | Review each reset root against its source and any external contract, or record it as unknown. Declare reviewed `reset_contracts` for external sources. Check recovery and removal, minimum pulse width and metastability outside the tool, and do not treat the absence of a finding as proof of power-intent safety. |
| Reset synchronisation | RDC-003, 006, 016, 030 | Review stage count, clock edge, reset source and attributes against the intended chain. Check recovery and removal timing and metastability in timing and physical sign-off. |
| Reset generation and glitches | RDC-007, 008, 009, 014, 017, 028 | Review each generated reset's clock, polarity and combining logic. Verify glitch freedom of gating and muxing logic by simulation or physical analysis, because RDC does not decide it from RTL. |
| Reset-domain data crossings | RDC-004, 005, 010, 020, 025 | Review the destination's tolerance of a source reset assertion and the reset sequencing for each crossing. Verify the functional behavior with reviewed assertions or simulation. The data crossing itself is a CDC question. |
| Asynchronous control conflicts and reset as data | RDC-019, 026 | Review set and clear priority and simultaneous assertion. Verify the behavior when reset and a functional control are active together, with simulation or assertions. |
| Sequencing and release order | RDC-011, 023, 024, CST-016 | Declare the release order in `reset_contracts`. The tool checks it structurally only against a recognised synchroniser chain. Verify full FIFO and handshake sequencing and request and acknowledge completion with simulation or assertions outside the tool. |
| Timing exceptions | RDC-027 | Review each exception on a reset root. Verify the crossing with reset-aware timing or simulation. An exception does not establish safety. |

A silent seeded functional defect fails qualification. Before authoring a blind batch, record
each class's claim and the expected behavior for bad, safe and unknown cases. The catalog maps
rules to these classes and `tools/dev/conformance.py` shows the coverage.

## Changing checks

Follow the six steps in [AGENTS.md](../../AGENTS.md) and the [CDC steps](CDC_WORKFLOW.md#changing-checks).
RDC specifics:

- **Shared model first.** Reset tracing through recognised synchronisers serves most RDC rules.
  Record changes in [ANALYSIS.md](ANALYSIS.md).
- **Reset intent is Assumed.** A `reset_contracts` entry changes a finding only as Assumed. Test
  it with a safe and an unsafe lookalike, such as a release order the chain does not enforce.
- **Physical behavior stays open.** Keep recovery, removal, pulse width and glitch behavior as
  obligations (`NOT_ATTEMPTED`), never as safe. Do not reuse retired IDs.

## Severity policy

| Severity | Use |
|---|---|
| FATAL | The run cannot produce a trustworthy result. |
| ERROR | Analysis establishes a structural RDC defect, such as a reset generated from another clock domain with an asynchronous relation (`RDC-007`) or one reset used with both polarities (`RDC-028` (a)). |
| WARNING | Potential reset defect, unproved recovery, removal or sequencing obligation, or Unresolved analysis. |
| INFO | Inventory disclosure or a recognized synchronizer or contract structure with stated assumptions. |

ERROR is emitted for `RDC-007` (Established for Async) and `RDC-028` (Established for (a)). Other RDC
rules are specified at WARNING or INFO.

An Unresolved finding or unproved obligation stays at least WARNING. The same contract applies
to [CDC](CDC_WORKFLOW.md#severity-policy) and [lint](LINT_WORKFLOW.md#severity-policy).

## Verification

Run `tools/dev/check.sh` for every change. It builds, runs CTest and checks the pinned public
RTL. Run standalone tools below under `tools/dev/mem_guard.sh` and in a scratch directory
outside the repository.

### Accuracy batch

Freeze, score once and keep the first score, as in the
[lint procedure](LINT_WORKFLOW.md#freeze-and-score-a-new-batch). The RDC budgets are
`tests/accuracy/rdc-budgets.json`, fixed before scoring with the same limits as CDC.

```sh
python3 tools/dev/accuracy.py --batch BATCH --strict-fresh-labels --freeze-batch freeze.json
python3 tools/dev/accuracy.py build/sverity --batch BATCH --strict-fresh-labels \
  --verify-freeze freeze.json --first-score --budgets tests/accuracy/rdc-budgets.json \
  --check --json first-score.json
```

Label rules follow the [CDC label rules](CDC_WORKFLOW.md#verification). An RDC or CST finding
counts when the readers name its rule. A bad label needs at least one rule able to detect the
defect, or a `detection_gap` reason agreed before scoring. Waived, resolved and not-analysed
findings earn no detection credit on bad cases.

After an adjudication, score regression without `--verify-freeze`, because the record changes
labels, and keep `--strict-fresh-labels`. A frozen `detection_gap` label cannot earn credit even
when a rule later reports the site.

Status: scored once (`rdc-8-rfc1-fresh`); see [Results](#results).

### Known-good corpus and reviewed residuals

Run the pinned public designs in [the manifest](../../tests/public_projects/manifest.json)
under the RDC intent, count non-INFO findings of deck rules per analysed kLOC and check FATAL,
ERROR and not-analysed budgets. The CDC runner
[`tools/dev/cdc_reviewed_residuals.py`](../../tools/dev/cdc_reviewed_residuals.py) is the model.
The structural run without reviewed reset intent must show 0 FATAL and 0 ERROR. Review every
ERROR on a known-good design as a possible false ERROR.

```sh
python3 tools/dev/cdc_reviewed_residuals.py build/sverity SCRATCH --deck rdc --json residuals.json
```

Status: recorded; see [Results](#results).

### Mutation tranche

`tools/dev/cdc_mutation_score.py` is the model for an RDC mutation tranche. A mutant counts as
caught only when its new finding names the required RDC or CST rule and endpoints, at WARNING,
ERROR or FATAL and established or heuristic. Freeze the operator list before the first score,
review each candidate blind for equivalence, and lint the mutated RTL with Verilator.

```sh
python3 tools/dev/cdc_mutation_score.py build/sverity SCRATCH --json mutation.json \
  --operators OPERATORS.json --profile asic --deck rdc \
  --sdc-dir tests/public_projects/reviewed_sdc
```

Operators: [`rdc-mutation-operators.json`](../../tests/accuracy/rdc-mutation-operators.json), 23
frozen and reviewed blind. Score with `--goal rdc`. `CST-016` (format) and `RDC-002` (b) have no
operator because no corpus input carries them.

Status: recorded; see [Results](#results).

### Capacity

Measure whole `top_earlgrey` with the exact top, files, constraints, toolchain and host, in a
separate scratch directory, and never beside a build or CTest run:

```sh
python3 tools/dev/capacity.py build/sverity SCRATCH --top top_earlgrey \
  --profile asic --goal rdc --deck rdc \
  --sdc tests/public_projects/reviewed_sdc/top_earlgrey.sdc \
  --library tests/public_projects/reviewed_sdc/top_earlgrey.cells.json \
  --exit-policy strict
```

Status: recorded; see [Whole-SoC capacity](#whole-soc-capacity). Record results as in the
[CDC procedure](CDC_WORKFLOW.md#capacity).

## Qualification

### Gate status

| Gate | Status |
|---|---|
| Deck of 40 rules | Met: 40 of 40 `validated` (`python3 tools/dev/conformance.py`). All 40 have every fixture shape and a written severity. |
| Reset intent (`reset_contracts`) | Met as specified: CST-016 reports each declaration as Assumed, and a release order is checked only against a recognised synchroniser chain. |
| Honest `COMPLETE` and strict exit 0 | Met. CLI test `cli_rdc_deck_clean_complete_vs_five_adverse_cases`: a clean design (one constrained reset, recognised synchronisers in two asynchronous domains, power intent declared, `RDC-013` waived) exits 0 `COMPLETE` under `--profile asic --goal rdc --deck rdc --exit-policy strict`. Five adverse cases each assert their own cause: no power intent (`RDC-012`, exit 6), `RDC-013` unwaived (exit 2), an unconstrained raw reset (`RDC-002`), two interacting reset sources (`RDC-011`) and a release-order contract the checker cannot verify (`CST-016`). Designs with two or more interacting reset sources stay INCOMPLETE through `RDC-011` (Phase 4 sequencing). |
| Known-good corpus | Met (results below) |
| Mutation recall and residuals | Met (results below) |
| Held-out accuracy | Met in verified regression: 9 of 9 budgets, 0 silently accepted critical defects. |
| Whole-SoC capacity | Completed, accepted over its original budget (results below) |
| Release package and disclosures | Open |
| Phase 4 RDC replacement | Open |

### Rule review

The deck rules were validated by independent adversarial AI review (Claude Opus 5.5, 2026-11-26),
not by human sign-off. The reviewer ran a frozen binary on safe, unsafe and unknown lookalike
probes for each rule. Every confirmed miss was fixed with a fixture. Reviewer, date and scope are in
each catalog entry. The review records these policy decisions:

- Reset contracts never change a finding's severity. A bound contract is cited as evidence and
  closes a finding only through a reviewed waiver.
- `RDC-025` findings on valid-qualified payload outputs are open obligations, because the consumer
  is outside the design, not false positives.
- `top_earlgrey.sdc` keeps `IO`, `IO_DIV2` and `IO_DIV4` asynchronous, as upstream
  `chip_earlgrey_asic.sdc` declares them. `RDC-007` findings between them are intended under that
  declared intent.
- Reset synchronisers behind the multi-bit scan clock mux are not recognised, and test-mode-only
  crossings stay ERROR for the user to waive (an `RDC-007` limit in [RDC.md](RDC.md)).
- An exported reset built by logic is reported by `RDC-009` as WARNING Unresolved (Heuristic when a register output selects between reset signals), not by a new rule.
- Power intent is a declaration, not a model: `power_intent.model = single_always_on` makes `RDC-012` INFO with a trust model, and any other model is a configuration error.
- A raw top-level reset whose every released register provably holds its reset value, or is masked at the next edge, is INFO under `RDC-003`. The proof assumptions are in [ANALYSIS.md](ANALYSIS.md).
- `RDC-011` is INFO when no interaction between reset sources remains, and WARNING Unresolved otherwise.

### Results

| Requirement | Result | Evidence |
|---|---|---|
| Rules independently reviewed | 40 of 40 deck rules passed the [rule review](#rule-review). | [catalog](catalog.json) |
| Blind mutation recall | 23 of 23 operators caught at first score (`--goal rdc`), and 23 of 23 in regression. `CST-016` and `RDC-002` (b) have no operator. | [first score](../../tests/accuracy/results/rdc-mutation-rfc1-first.json), [regression](../../tests/accuracy/results/rdc-mutation-rfc1-regression.json), [operators](../../tests/accuracy/rdc-mutation-operators.json) |
| Reviewed residuals | 5 of 5 budgets on 2.894 kLOC under reviewed SDC: 0 FATAL, 0 ERROR, 0 not-analysed, 58 non-INFO deck findings (29 heuristic, 19 Unresolved, 10 informational-confidence WARNING), obligations 6.57 per kLOC. Review of every finding: 37 contract-dependent unknown, 18 open obligation, 3 coverage gap, 0 confirmed false positives (budget at most 1 per kLOC). Counting the 3 `RDC-025` payload findings on `axis_async_fifo` as noise would give 1.04 per kLOC. | [residuals](../../tests/accuracy/results/rdc-reviewed-residuals-rfc1.json), [residual review](../../tests/accuracy/results/rdc-residual-review-rfc1.json.gz) |
| Held-out accuracy `rdc-8-rfc1-fresh` | 45 blind cases. First score: missed 0.158, false ERROR 0.062, false alarm 0.312, unresolved bad 0.00, unresolved good 0.688, review good 1.00, silent unknown 1, silent critical `rfc1-b01`, `b02`, `b08`. Verified regression: missed 0.00, unresolved bad 0.05, false ERROR 0.00, false alarm 0.08, unresolved good 0.00, silent unknown 0; 9 of 9 budgets met, 0 silently accepted critical defects. | [freeze](../../tests/accuracy/results/rdc-8-rfc1-fresh-freeze.json), [first score](../../tests/accuracy/results/rdc-8-rfc1-fresh-first-score.json.gz), [adjudication](../../tests/accuracy/results/rdc-8-rfc1-fresh-adjudication.json), [regression](../../tests/accuracy/results/rdc-8-rfc1-fresh-regression.json.gz) |

Adjudication after the score ([record](../../tests/accuracy/results/rdc-8-rfc1-fresh-adjudication.json)): an independent AI judge moved `rfc1-a04`, `b11` and `c06` from good
to unknown under one convention: a good case is unknown when its RTL is correct only under an
undeclared property of a top-level input. It reviewed `b04`, `b05`, `b06` and `b10` and kept them
good. It also moved the frozen detection gaps of `b01` and `b02` to expected rule `RDC-009`, which now
covers exported resets. These are AI decisions, not human sign-off.

The remaining good-case finding is `rfc1-b10` (`RDC-002`, `003`, `011` and `RDC-026` on an asynchronous
set and clear of one bit). It follows the specification.

The [accuracy index](../../tests/accuracy/results/README.md) lists every batch.

### Whole-SoC capacity

Whole `top_earlgrey` (OpenTitan `92f3d03b`, reviewed SDC and cells, `--goal rdc --deck rdc
--profile asic`, strict) runs to completion, over the RFC target of 600 s and 8 GiB. As with CDC's
accepted 730 s and 11.9 GiB, whether this is acceptable is the maintainer's decision.

Measured 2026-11-26 on the current binary:

| Item | Value |
|---|---|
| Design | OpenTitan `top_earlgrey` at `92f3d03b0dcf6d5c1933a65d1497747f67d19f4c`, the CDC capacity file list of 733 files |
| Intent | [`top_earlgrey.sdc`](../../tests/public_projects/reviewed_sdc/top_earlgrey.sdc) and [`top_earlgrey.cells.json`](../../tests/public_projects/reviewed_sdc/top_earlgrey.cells.json) |
| Command | The [capacity](#capacity) command above, under an 11 GiB cap |
| Run | Completed in 783 s, 8.15 GiB peak RSS. Execution SUCCESS, coverage INCOMPLETE, review FAIL, strict exit 5. |
| Size | 3,287,139 nodes, 15,618,508 edges, 237,159 register bits, 38 clock domains |
| Findings | 0 FATAL, 59 ERROR (40 `RDC-007` diagnostics), 34,676 WARNING, 5,663 INFO. Report SHA-256 `9249b6c73be685191b98a77504e6b35bc5dbd96f6a46b96666657ae3cbcb1e97` |

Independent review grouped the `RDC-007` ERRORs. Most are real asynchronous releases that OpenTitan
makes safe by clock gating or sequencing the tool cannot prove. A few are real crossings without
that protection (the `spi_device` pulse synchroniser source level and the AST `rng` enable), and
two are test-mode-only paths through the scan clock mux (a documented limit). The WARNINGs are not
triaged.

The whole-SoC strict exit is 5 (coverage INCOMPLETE, review FAIL): `top_earlgrey` has interacting
reset sources, which stay INCOMPLETE through `RDC-011`.

The target was 600 s and 8 GiB. The maintainer accepted 783 s and 8.15 GiB for 1.0 on 2026-11-26.

### Still open

For the 1.0 release, and not specific to RDC: reference crosswalks, governance and provenance,
the published frozen deck list with its severity and waiver matrix, and the mandatory
disclosures. These are that independent AI review is not human sign-off, that no commercial
comparison was made, and the out-of-scope list above. The
[RFC acceptance criteria](../SVerity_RFC.md#8-acceptance-criteria-for-10) govern the release
claim.

For RDC 1.0, only the release package and disclosures remain, shared with the 1.0 items above.
A faster whole-SoC run stays a [roadmap](../ROADMAP.md#2-mid-term-reaching-10) item.

For Phase 4 RDC replacement, per the [roadmap](../ROADMAP.md#2-mid-term-reaching-10): reset
trees, independently asserted resets, user-declared sequencing, release order, and a model of
a stopped clock and a held or released reset. The roadmap's gate is that sequencing can be
stated and checked, not only reported as unresolved. Until then, `RDC-011`, `RDC-013` and the
physical obligations stay open.
