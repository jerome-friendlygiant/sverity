# Lint workflow

[Scope and gate](#scope-and-gate) explains what CI checks. The [defect classes](#defect-classes)
define what the lint deck covers. To change that coverage or tune a rule, follow
[Changing checks](#changing-checks), then use [Verification](#verification) and
[Qualification](#qualification) to decide what evidence to collect.

## Scope and gate

**W-LINT-1** is pre-merge lint for synthesizable ASIC SystemVerilog. Run it per IP with
its top, files and configurations. The reference is the public Verilator lint stage
in [pinned OpenTitan](https://github.com/lowRISC/opentitan/tree/92f3d03b0dcf6d5c1933a65d1497747f67d19f4c).
Style, DFT, FPGA conventions, gate-level netlists, testbenches and commercial lint
policies are outside this workflow.

| Result | Default gate |
|---|---|
| Unwaived ERROR, FATAL, or established or heuristic WARNING | Blocks |
| Execution failure or lost analysis in the run | Blocks |
| Declared lint obligation or other lint finding classified as not analysed | Counted and disclosed. Blocks in strict mode or when new against a baseline. |
| INFO | Advisory |

A lint obligation must name a predicate declared in the catalog. An undeclared
obligation ID still blocks. Review waivers with an owner, rationale and the narrowest
applicable scope. See the [tutorial](../TUTORIAL.md) for commands and baseline behavior.

## Defect classes

A critical class can ship a functional defect. Every such class needs an explicit
detection or disclosure contract. The catalog maps checks to these classes and
`--deck lint` selects that coverage claim. `tools/dev/conformance.py --classes`
shows the mapping.

| Class | Defect | Critical |
|---|---|---|
| `LC01` | The design does not compile or elaborate: syntax, unresolved names, illegal port connections | yes |
| `LC02` | A value is lost or changed by width: truncation or extension at an assignment, port, operator, shift, carry or constant | yes |
| `LC03` | Signedness mixing changes a result: comparison, extension or arithmetic | yes |
| `LC04` | Out-of-range select or index | yes |
| `LC05` | An implicitly declared net (a typo) | yes |
| `LC06` | An undriven signal, bit range or output | yes |
| `LC07` | Multiple or conflicting drivers | yes |
| `LC08` | An input or inout port left unconnected or connected empty | yes |
| `LC09` | An unused declaration | no |
| `LC10` | A combinational loop | yes |
| `LC11` | An unintended latch, or a combinational value not assigned on every path, including a task or function output | yes |
| `LC12` | Procedure discipline that makes simulation differ from hardware: blocking writes to state, nonblocking in combinational logic, mixed kinds, incomplete sensitivity, combinational reads before writes, cross-process races | yes |
| `LC13` | Case and FSM coverage: uncovered values, overlapping or unreachable items, wildcard or X-containing items, violated `unique`/`priority`, unhandled, unassigned or stuck enum states | yes |
| `LC14` | X or Z misuse: X assigned in design logic, X or Z compared or used as a condition | yes |
| `LC15` | A construct synthesis cannot build or builds differently: delays, `force`, simulation types and system tasks, foreign (DPI) code, unbounded loops, initial values on ASIC | yes |
| `LC16` | Reset behaviour: reset-only assignment, asynchronous control polarity that does not match its edge, a non-constant reset value, state read in a condition before it has any defined value | yes |
| `LC17` | Clock or asynchronous control misuse, including a clock used as data or one register on both edges | yes |
| `LC18` | Dead logic: constant conditions, unreachable branches or generate alternatives, a write always replaced before any read | no |
| `LC19` | Declaration hygiene: shadowing, unused or duplicate imports and packages, missing `default_nettype none` | no |
| `LC20` | A memory with several writers or a non-clocked writer | yes |
| `LC21` | Arithmetic that is wrong by construction: a constant division by zero, a comparison that is always true or false | yes |
| `LC22` | An output port left unconnected | no |
| `LC23` | Reset consistency: a register without a reset value, or a reset-free block where its peers reset | no |
| `LC24` | Memory read behaviour that depends on the implementation (read during write, read port style) | yes |
| `LC25` | Synthesis pragmas that can change hardware behavior, including `full_case`, `parallel_case` and `translate_off` | yes |

## Changing checks

1. **Name the hardware problem.** Pick its class and look for an existing predicate in
   [LNT.md](LNT.md). Add a class only if the workflow needs a distinct outcome.
2. **Change the shared analysis first.** Reuse facts about drivers, widths, flow and
   hierarchy. Record any changed assumptions in [ANALYSIS.md](ANALYSIS.md).
3. **Set the contract.** Update the rule title and spec with its trigger, exemptions,
   severity and unknown outcome. Keep the same ID for the same predicate. When
   splitting, merging or removing checks, document where the covered behavior went.
   Retire IDs and never reuse them.
4. **Make policy tunable where it varies by design.** Use severity overrides for
   review policy, rule options or target profiles for semantic choices, and scoped
   waivers for exceptions. State the default in the rule spec. Preserve disclosure
   for opaque code and unsupported inputs.
5. **Update the catalog and fixtures with the code.** Record status, deck, classes,
   gaps, obligation declarations and review metadata there. Keep titles and rule
   explanations in the spec. Use existing fixtures as regression guards and add a
   minimal case for each newly covered failure mode. Check safe lookalikes and
   unknown inputs.
6. **Verify and review.** Run the repository gate. Have an independent reviewer run
   minimal adversarial cases before marking the rule validated.

## Severity policy

| Severity | Use |
|---|---|
| FATAL | The run cannot produce a trustworthy result. |
| ERROR | Analysis proves the hardware cannot be built or is wrong. |
| WARNING | Potential hardware defect, simulation mismatch, or Unresolved analysis. |
| INFO | Advisory policy or a recognized construct that synthesizes as written. |

An Unresolved finding stays at least WARNING. Document any advisory exemption in
the rule spec and preserve upstream severity when lowering a slang warning.

## Verification

Run `tools/dev/check.sh` for every change. When a change can affect workflow coverage,
noise or capacity, collect the relevant evidence below.

### Freeze and score a new batch

1. **Choose coverage and budgets.** Include bad, safe lookalike and unknown cases
   where meaningful for each critical class. Agree on budgets before scoring.
2. **Label blind.** Authors and two independent readers work from the defect
   classes. Resolve disagreements about verdict, class or site with a third reader.
3. **Freeze the labelled inputs** before running the analyzer on them.
   ```sh
   python3 tools/dev/accuracy.py --batch BATCH --strict-fresh-labels --freeze-batch freeze.json
   ```
4. **Score once** to a new file and preserve the output, including failures.
   ```sh
   python3 tools/dev/accuracy.py build/sverity --batch BATCH --strict-fresh-labels \
     --verify-freeze freeze.json --first-score --budgets tests/accuracy/budgets.json \
     --check --json first-score.json
   ```
5. **Record what the score supports.** Report per-class denominators, misses, noise,
   unresolved outcomes and execution failures. Record any later label adjudication.
   Use subsequent scores on the same cases as regression evidence.

The manifest pins batch membership and bytes. Blindness and chronology depend on
the review record. The maintainer owns [accuracy budgets](../../tests/accuracy/budgets.json).
LC24 uses disclosure as its class contract. A silent critical defect or unknown,
or a false error on a good case, fails qualification.

### Check the reference IPs

| Check | What to verify |
|---|---|
| `tools/dev/reference_lint.py build/sverity SCRATCH` | Replay each pinned IP and its Verilator configuration. Review migration failures and preserve each waiver's file, line, message and hierarchy scope. |
| `tools/dev/verilator_diff.py SCRATCH` | Review unmatched sites in both directions against the pinned RTL. Use the Verilator version required by the reference checkout. |
| `tools/dev/noise_gate.py SCRATCH` | Triage every blocking or not-analysed site. Explain vanished findings and record the owner, rationale and disposition of residuals. Treat setup and execution failures as open work. |
| `tools/dev/mutation_recall.py build/sverity SCRATCH --replay REPLAY_TREE` | Measure detections against the unmutated run. Report invalid and equivalent mutants separately and state which classes have mutation operators. |
| Capacity | Measure each IP with elaboration and reporting included. Target 120 s and 8 GiB per IP. Record peak memory, elapsed time, coverage and report differences. |

The reference noise targets are provisional. Per 1,000 analysed source lines, allow
at most two not-analysed findings, five obligations and one confirmed false positive.
Resolve or explain every residual site. Publish aggregate evidence and keep raw vendor RTL
and tool reports outside the repository.

## Qualification

| Requirement | Published evidence |
|---|---|
| Checks that can emit ERROR or FATAL have written severity policies and independent review | Individual spec and catalog review entries. Independent AI reviewers ran minimal unsafe and safe lookalikes, including severity and gate bypasses, interface drivers, flow, widths, case semantics, resets and memory. |
| Independently labelled accuracy | Two 120-case batches preserve the original first scores and recorded adjudications. After fixes, both meet 10 of 10 budgets with zero silently accepted critical defects in regression. |
| Residuals reviewed by predicate and site | The ten-IP gate records 3 blocking findings, 130 not analysed and 294 obligations. |
| Recall and capacity | 435 of 436 valid seeded mutants detected, with one equivalent mutant. The ten-IP replay took 14.5 s and 0.6 GB peak. Lint-only `top_earlgrey` took 75 s and 5.0 GB peak, with identical reports under 7 and 6 GiB caps. |

The [artifact index](../../tests/accuracy/results/README.md) links the measurements
to their inputs. Reviews and label checks used AI reviewers. A human label check and
a real team's switching trial were deferred for 1.0. A future trial should use a
required per-IP gate for at least 20 merges over four weeks and record triage effort.
