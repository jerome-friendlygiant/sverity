# SVerity — SystemVerilog Static Analysis RFC

**Project:** SVerity (`sverity`) · **Status:** specification; implementation state is `sverity --list-rules`  
**Mission:** an open-source, vendor-independent, self-contained, high-performance SystemVerilog analyzer for lint, CDC, RDC and
progressively stronger correctness analysis; long term, a community-maintained industry-standard tool.  
**Scope of 1.0:** lint (`LNT`), CDC, RDC and the constraint/completeness checks they depend on (`CST`). Style (`STY`) and SVA lint (`SVA`) are outside
this scope (they add noise and keep every run `INCOMPLETE`); see section 10 for removing them from the catalog.

Rule inventory: [`docs/rules/catalog.json`](rules/catalog.json) (machine-readable; the sign-off / release list: authoritative for IDs and
per-rule `status`) and the per-family tables [`docs/rules/*.md`](rules/) (human-readable titles and, as rules get specified, their
trigger/exemption/severity text). A unit test keeps the two in sync.

## 1. Product charter

- A **native, offline C++20 CLI and embeddable library** using **slang** as the sole mandatory SystemVerilog frontend. No
  Python (runtime), second parser, simulator, formal solver, online service or commercial licence at runtime. (Building
  slang needs Python 3 for its code generator.)
- **Complete in-scope lint, CDC and RDC coverage for every advertised release**: structural and synthesizability lint, source/destination
  domain analysis, known synchronisation topologies, constraint coverage, reset-domain safety.
- **Own implementation of every advertised check.** No wrappers that silently delegate to Verible, Verilator, SpyGlass or other
  tools; borrow concepts/code only with provenance (licence audit) and tests.
- **ASIC and FPGA** via profile libraries (cells, clock-generation and synchroniser primitives, initialisation assumptions,
  constraint dialects); semantic checks are shared.
- **Never silently hide findings or limitations.** Emit every INFO/WARNING/ERROR/FATAL. When a result is unknown, unsupported,
  unproved or only heuristically classified, emit at least a WARNING requiring review, keep the evidence, and mark the
  analysis scope INCOMPLETE.
- **Be explicit about what RTL cannot show.** Unknown clock relationships, unmodelled macros, missing timing evidence or an
  unproved protocol are *unresolved / proof-needed*, never "safe". Metastability MTBF and timing closure cannot be proven from
  digital RTL.
- **Correctness and published coverage define success**, not speed and not what other tools implement.
- Out of scope: simulation, UVM execution, DFT, physical implementation, STA signoff, ATPG.

**Formal-assisted analysis (future goal).** The shared model is meant to be reusable as the front end of a transition-system
representation for proof of handshake/FIFO protocols, Gray-code transitions, FSM reachability and reset sequencing. Proof
status, assumptions, bounded/unbounded scope, counterexamples and inconclusive results must be representable
(`PROVEN | COUNTEREXAMPLE | UNKNOWN | NOT_ATTEMPTED`); a solver is never a dependency of the structural core.

## 2. Architecture

```text
SystemVerilog + filelist + parameters + constraints + profile
        │
   slang (preprocess, AST, hierarchy, types, diagnostics)   ── every slang diagnostic is preserved
        │
   Model: bit-level dependency graph · registers · clocks · resets · black boxes · recorded precision fallbacks
        │
   Rules: slang-backed · graph lint · CDC · RDC · constraints · (proof, future)
        │
   Report: evidence · waivers · coverage · proof obligations → console, JSON, SARIF, exit code
```

One elaboration and one model per run; each check is a rule class or a shared analysis pass, never an independent parse.
Future proof engines consume the same model through explicit interfaces.

Extension: embedders add rules with `sverity::registerRule()` (`include/sverity/rule.h`); AST-based rules derive from `sverity::lint::LintRule`
(`include/sverity/lint.h`, examples in `src/rules/lint/`). A custom rule outside the catalog is run and listed in the coverage record as `custom`.

Reference-only sources (study, adapt or reimplement with provenance; never runtime dependencies):
[slang-tidy](https://www.sv-lang.com/tools.html), [slang-netlist](https://github.com/jameshanlon/slang-netlist),
[OpenCDC](https://github.com/Ayoubbelguellaoui/CDC), [RTL Buddy CDC](https://github.com/rtl-buddy/rtl-buddy-cdc),
[Verible](https://chipsalliance.github.io/verible/verilog_lint.html),
[Verilator](https://verilator.org/guide/latest/warnings.html),
[Vivado CDC](https://docs.amd.com/r/en-US/ug906-vivado-design-analysis/Understanding-the-Clock-Domain-Crossings-Report-Rules),
[Altera CDC/RDC](https://docs.altera.com/r/docs/683369/current/an-919-improving-quality-of-results-with-design-assistant/clock-domain-crossing-and-reset-domain-crossing-rules).

## 3. Diagnostic contract (non-negotiable)

| Severity | Meaning |
|---|---|
| `INFO` | Context, statistics, advisory findings, and recognised structures whose assumptions are stated (a trust model). Never an unproved property. |
| `WARNING` | Potential defect, heuristic, unproved property (proof state `NOT_ATTEMPTED` without a stated trust model), incomplete knowledge or unsupported analysis. If safety cannot be determined, at least this. |
| `ERROR` | Established violation of a specified rule, or an invalid design/constraint. |
| `FATAL` | Analysis cannot reliably continue (e.g. the design does not elaborate). Report what was found without claiming completeness. |

Each diagnostic has: stable rule ID, severity, message, file/line/column, hierarchy, evidence/path, affected signals, confidence
or proof state, disposition, next step. A rule's severity policy must be specified before the rule is `validated`; do not
invent a severity where semantics are undetermined.

- **No silent filtering.** The JSON report contains everything, including INFO and waived items. The console may truncate only
  with an announcement, and only when a complete report was also written. Only *exact* duplicates are grouped, and
  occurrences and counts are preserved.
- **Waiver ≠ deletion.** A waived finding keeps severity, evidence and identity and is shown as `WAIVED`. A waiver needs an exact
  rule ID, a reason, an owner and at least one non-wildcard narrowing field (fingerprint, file, hierarchy or signal), may
  expire, matches per occurrence, and is rejected (with a warning, finding stays open) when expired, stale, overbroad, malformed
  or duplicated. Waivers never turn uncertainty into SAFE.
- **Unknown is not safe.** Missing constraints, unresolved primitives, precision fallbacks, failed passes, unsupported
  constructs, unimplemented rules and unproved properties yield a WARNING, `REVIEW_REQUIRED`, and an INCOMPLETE scope.
- **Preserve upstream diagnostics.** slang errors/warnings are never discarded; every way slang could drop a diagnostic
  (`-Wno-*`, suppress-warnings options, `--waiver-file`, `pragma diagnostic`, error limits, options hidden in filelists) is
  detected and recorded as a coverage gap or warning. All slang warnings are enabled by default (`-Weverything`).
- **Dispositions:** `OPEN`, `REVIEW_REQUIRED`, `RESOLVED`, `WAIVED`, with optional reviewer decision/comment; a review decision
  is not a formal proof.
- **No misleading green summary.** Report counts of executed/partial/disabled/unimplemented rules and evaluated design objects.

**Three orthogonal result dimensions** (never collapsed into one pass/fail):

- `execution_status`: `SUCCESS` | `ERROR` (the tool could/could not run as expected; a FATAL, e.g. an elaboration failure, is `ERROR`).
- `analysis_coverage`: `COMPLETE` | `INCOMPLETE` (advertised checks and model assumptions adequately evaluated). Any rule that is
  partial, unimplemented, disabled or failed makes it `INCOMPLETE`.
- `review_status`: `PASS` | `REVIEW_REQUIRED` | `FAIL` (no open issues / unresolved warnings / unwaived ERROR or FATAL).

**Exit code** (bit mask, strict policy): 1 = `FAIL`, 2 = `REVIEW_REQUIRED`, 4 = `INCOMPLETE`, 8 = execution `ERROR`, 64 = usage
error. `--exit-policy violations` keeps only bits 1 and 8. The full disposition is always in the report.

**Output formats.** JSON is canonical and deterministic (paths relative to `--root`, stable fingerprints, valid UTF-8). SARIF
maps FATAL/ERROR → `error`, WARNING → `warning`, INFO → `note`, with exact severity, disposition, confidence and proof state in
`properties`, and waived findings as SARIF suppressions (still emitted).

**Formal-proof extension contract.** A check may return `PROVEN`, `COUNTEREXAMPLE`, `UNKNOWN` or `NOT_ATTEMPTED`, recording
assumptions, engine/version, bounded vs unbounded, timeout and witness. `UNKNOWN`/timeout is a WARNING; bounded success or an
unattempted proof is never reported as unbounded proof.

## 4. Rules

The catalog is an **auditable, growing, versioned inventory**, not a claim of universal exhaustiveness. Every catalogued rule is
native, specified, tested and tracked; an incomplete rule stays visible.

| Family | Scope |
|---|---|
| `LNT` | Semantic, structural and synthesizability lint |
| `STY` | Out of 1.0 scope (formatter territory); to be removed, the few semantic items move to `LNT` |
| `SVA` | Out of 1.0 scope (needs a property engine); to be removed |
| `CDC` | Clock-domain crossings, synchroniser/protocol structure |
| `RDC` | Reset-domain crossings, async reset behaviour and coordination |
| `CST` | Constraints, analysis completeness, trust models, report integrity |

**Rule states.** Catalog status: `proposed → specified → implemented → validated` (or `blocked`). At run time a rule is
`EXECUTED` (the whole title), `PARTIAL` (a documented subset; the catalog's `gap` field and the run's coverage
record say what is missing, and a partial rule without a `gap` fails the tests), `DISABLED`, `UNIMPLEMENTED`, `NOT_RUN` or `FAILED`. No rule is done because another tool implements it. Execution
waves (`foundation`, `breadth`, `advanced`) only order the work.

**Conformance record** (target shape per rule; today the catalog carries id, title, status, wave, an implementation hint and, for
partial rules, the gap). A release freezes the set of rules that must be `validated`; the remaining fields below are added to a
rule's entry when it is specified:

```json
{ "id": "CDC-003", "status": "proposed", "inputs": ["elaborated_design", "clock_constraints", "connectivity"],
  "trigger": "...", "exemptions": ["recognized_synchronizer_with_stated_assumptions"],
  "tests": ["good", "bad", "boundary", "unknown", "hierarchy"], "severity_policy": "must_be_specified_before_validation",
  "unknown_handling": "emit_warning_and_require_human_review",
  "evidence": ["launch_register", "capture_register", "data_path", "domain_provenance"],
  "proof_obligations": ["crossing_protocol_validity_when_applicable"] }
```

Required native coverage includes: X/Z, signedness, sizing and synthesis-vs-simulation hazards; FSM and
static semantic analysis with an explicit unknown/proof-obligation distinction; CDC (synchronisers, multi-bit coherency,
reconvergence, pulse/toggle/handshake/FIFO, generated/gated/muxed clocks, constraint validity); RDC (multiple resets,
assertion/release, synchronisers, reconvergence, sequencing, CDC/RDC interaction).

**Constraints.** An SDC subset is interpreted (`create_clock`, `create_generated_clock`, `set_clock_groups`, false-path /
multicycle / min / max-delay). Every other command is disclosed (CST-009), never silently dropped. A clock relationship is
`same`, `asynchronous`, `exclusive` or **unknown**; unknown stays unknown.

## 5. ASIC versus FPGA

One compiler, one model, one rules engine, with profile libraries for (a) clock-generation and gating cells, (b) synchroniser/FF
macro recognition, (c) reset and initialisation semantics, (d) timing-constraint dialects, (e) implementation assumptions. An
unsupported primitive is *unresolved*, not assumed safe. No blanket statements like "FPGAs do not need RDC" or "2FF is
universally safe".

## 6. Development stages (ordering, not deferral)

| Stage | Deliverable | Gate |
|---|---|---|
| Foundation | build, slang integration, diagnostics, provenance | deterministic offline build, public-RTL elaboration tests |
| Semantic graph | bit/range dependency graph, registers, path evidence | good/bad/parameter/generate/hierarchy tests, documented fallbacks |
| Lint breadth | `LNT` | every supported rule individually verified |
| Clock/reset framework | clock extraction, SDC subset, reset modelling, black boxes, completeness | unknown/unresolved cases fail safe |
| CDC + RDC breadth | full structural/protocol recognition and reset-domain checks | good/bad/boundary/constraint/unknown cases per rule |
| Conformance release | frozen baseline deck, public-design and mutation tests | published severity/waiver/review matrix, reproducible benchmarks |
| Formal-assisted | property IR, solver adapter, obligations, counterexamples | reproducible proven/failed/unknown outcomes, no false proof claims |

A milestone may be called *preview* with incomplete rules. A **1.0 "complete to published specification"** claim requires every
frozen rule `validated`, or a transparent scope revision *before* freezing.

## 7. Testing

- **Fixtures per rule** (`tests/fixtures/<RULE-ID>/`): good, bad, boundary, unknown/unconstrained, hierarchy; plus report-integrity
  checks on every fixture. This is the required shape for `validated`; not every implemented rule has all of it yet (section 9). Fixtures are annotated SystemVerilog files (see the runner), so adding a case needs no C++. Code map: `docs/ARCHITECTURE.md`.
- **Mutation tests are mandatory** for CDC/RDC: bypass a synchroniser stage, add logic between stages, fan out the first stage,
  synchronise bus bits independently, break Gray pointers, change constraints, flip reset polarity, break reset release, introduce
  a partial drive, violate sizing/case rules. Each injected defect has expected findings, and reverting it clears them (fixtures carry `// revert: <old> => <new>` and `// revert-expect-none:` annotations; the runner analyses the reverted variant too).
- **Public projects**, pinned by URL and commit, never vendored, never assumed defect-free. Two roles: robustness/performance runs
  (PicoRV32, Ibex, OpenTitan) and a **known-good CDC corpus** (PULP `common_cells` CDC blocks, Forencich `verilog-axis` async FIFO, with the
  constraints they need) on which a correct, declared-asynchronous design must produce no ERROR; every false positive found there becomes a
  fixture. sv-tests is frontend only, not a CDC oracle.
- **Differential evaluation** against slang, Verible, Verilator and documented vendor rules is allowed but never replaces native
  rule-level tests; a commercial tool's output is not automatically ground truth.

## 8. Acceptance criteria for 1.0

- C++20 + slang; offline reproducible build from pinned sources; no Python/other tools at runtime.
- Deterministic CLI/API, JSON and SARIF; stable rule IDs; every finding visible; ASIC/FPGA profiles; explicit clock/reset/black-box contracts.
- Reference crosswalk (each relevant rule of slang, slang-tidy, Verible, Verilator, AMD, Altera mapped to a native rule or a justified exclusion) reviewed.
- All frozen rules `validated` with a written trigger, exceptions, severity policy and good/bad/boundary/unknown tests; zero undocumented disabled/TODO rules.
- Full clock/reset/constraint/graph coverage accounting; no silent "unknown is safe"; proof obligations stay explicit.
- Curated public-project and mutation suites; documented false-positive/negative investigations; reproducible benchmarks (fixed hardware and compiler versions).
- Governance: rule proposal template, independent maintainer approval, licensing/provenance, security disclosure policy.

"Industry standard" is a community adoption goal, not something the project can confer on itself.

## 9. Known defects and gaps (independent review, 2026-11)

A reviewer built the tool, ran the suite (all passing) and ran it on hand-written cases and public RTL (PicoRV32, Forencich `verilog-axis`). Confirmed:

1. **False ERROR on a correct synchroniser (blocker).** A 2-flop synchroniser whose reset is a trailing override is reported as CDC-003 and CDC-028
   ("no recognised synchroniser", "qualified by a clock enable"); the reset-first and no-reset forms are recognised.
   ```systemverilog
   always_ff @(posedge clk_b) begin b1 <= q_a; b2 <= b1; if (rst_b) begin b1 <= 0; b2 <= 0; end end
   ```
   `axis_async_fifo` from `verilog-axis`, with its two clocks declared asynchronous, gives 25 ERRORs (Gray pointers, multi-stage status synchronisers) that
   inspection showed to be false. The reset-override must be seen as a synchronous reset, not as an enable or link logic.
2. **Output is not path-independent.** Messages contain absolute paths (seen in LNT-066) even with `--root`, and the fingerprints of those findings differ between checkouts, so a
   fingerprint waiver does not carry from a developer machine to CI.
3. **Mislabelled or duplicated findings.** CDC-032/CDC-009 count "bits" when one bit fans out to several synchronisers; CDC-013 says "faster" when the period is only comparable;
   one broken chain is reported once per stage (c1 and c2) by CDC-003.
4. **Noise and no clean exit.** A trivial clean design exits non-zero under the strict policy (always `INCOMPLETE`, RDC-012/RDC-013 always warn). RDC-005 fires on every
   synchroniser between differently reset domains. LNT-025 (default-then-override) gives 1294 findings and STY-011 2590 on PicoRV32. Lint-only runs still emit CDC/RDC warnings (no goal selection).
5. **Configuration is unreachable.** `Config::ruleOptions` (e.g. `CDC-005.min_stages`) has no CLI or file input, severities cannot be overridden, and `--disable-rule`
   takes exact IDs only.
6. **Constraints.** Real SDC idioms (`set`, `$var`, `foreach`, `all_inputs`) are rejected or disclosed-but-unapplied; `set_output_delay`, `set_case_analysis` are not interpreted.
   No SGDC, UPF or SDF input. CDC-012's cell-metadata half needs ASIC/FPGA profile bundles that are undefined.
7. **Test shape.** Of the implemented rules, about 52 have no "must not fire" assertion and 19 of the CDC/RDC rules have no `// revert:` mutation, against section 7. Nothing is `validated`; `default_severity` is null.
8. **Scale (measured).** About 19 MB per PicoRV32 copy, linear: 42k register bits 10 s / 390 MB, 170k bits 37 s / 1.3 GB (about 64 edges per node; arithmetic-heavy RTL).
   Signals over 16384 bits collapse to one node. No hierarchical or abstracted-block flow, so a full-chip run is flat. Interfaces and user functions are not followed (recorded as fallbacks).
9. **Shared analysis limits** (unchanged): `Flow` walks loops once and treats user calls and mid-body timing controls as opaque; guards are not kept as AND/OR trees;
   X/Z values are not propagated through variables; LNT-044 and LNT-066 are approximate; slang's own case-coverage and `continue` handling are approximate (LNT-013 reports Unresolved).

Not detected as wrong in review: crossings through interfaces, functions, hierarchical references, structs, generate loops, memories, latches and clock muxes were all reported; reconvergence, first-stage fan-out,
mixed edges, third-clock stages and pulse loss were caught. This is evidence from a few dozen cases, not a proof of absence of false-safe results. Two earlier internal audits (CDC/RDC false-safe hunt, flow/model/library foundations) are kept as `audit_*` fixtures.

Inherent limits (not defects): RDC-029 needs UPF/CPF; CDC-024, LNT-071, RDC-013 and CST-024 cannot be decided from RTL and only emit unknown/obligation results. Vendor rule lists (AMD UG906, Altera AN 919) could not be fetched,
so those crosswalk entries are UNVERIFIED; commercial rulesets (STARC, OpenMORE, SpyGlass/VC SpyGlass rule references) are not public and are not used. Rule scoping splits still to apply: LNT-019, LNT-033, LNT-049, LNT-059;
LNT-042's overlap clause belongs to LNT-009; one obligation emitter per structure (CDC-024); one primary finding per unsynchronised bus (CDC-016/017/028/032 versus CDC-003/004).
Retired (merged, never reused): LNT-021 → RDC-028, LNT-023 → LNT-003/057, LNT-028 → CST-005, LNT-030 → STY-* (now out of scope), LNT-043 → LNT-025, LNT-046 → LNT-047, LNT-052 (dropped: an inferred enable has no defect semantics),
CDC-011/036 → CDC-010, CDC-021 → RDC-007, CDC-022 → CST-004/CDC-038, CDC-025 → CDC-008, CDC-027 → CDC-007, CDC-033 → CDC-008/018, CDC-042 → CDC-013, RDC-015 → RDC-003/007, RDC-018 → RDC-008, RDC-021 → RDC-026, RDC-022 → RDC-007.

## 10. Next steps, in order

1. **Remove `STY` and `SVA` from the catalog, family docs and fixtures** in one commit (history keeps them). Move the semantic items to `LNT` (`default_nettype none`, plain `always` versus `always_ff`/`always_comb`). Trim the unimplemented `CST` rules that duplicate a CDC/RDC title.
   Re-state the 1.0 definition as LNT + CDC + RDC + the CST checks they need.
2. **Fix defect 1** and add the trailing-reset form, plus the `axis_async_fifo` case, as fixtures; then run the known-good corpus (section 7) as a CI gate with zero ERROR.
3. **Fix defect 2** (relative paths in messages, fingerprints independent of checkout) with a fixture that runs from two directories.
4. **Make it usable:** a config file (rule options, severity overrides, wildcard enable/disable, goal presets `lint`/`cdc`/`rdc`); a default policy under which a clean design exits 0; grouping of repeated findings by root cause; lower default severity or opt-in for LNT-025, RDC-005, RDC-012/013.
5. **Constraints:** a Tcl-subset SDC evaluator (`set`, `$var`, `foreach`, `all_inputs`, `source`), `set_output_delay`, `set_case_analysis`; then a UPF subset; an SGDC importer only from legitimately obtained documentation.
6. **Reports and onboarding:** per-rule summary, HTML report, `--explain <RULE>`, in-source waivers, waiver-template generator, a tutorial, and a migration guide from SpyGlass/VC SpyGlass using public rule names only.
7. **Distribution:** CI (build, ctest, known-good corpus), prebuilt binaries or a container (a source build takes about 35 minutes on 4 cores).
8. **Validate a narrow core** (2-flop/Gray-FIFO CDC, reset synchronisers, main structural lint): written severity policy, good/bad/boundary/unknown/mutation fixtures, independent adversarial review; claim only that scope until the rest follows.
9. **Scale and hierarchy:** profile bytes per edge, add abstracted block models, and decide the large-symbol policy.
10. **Governance:** more than one maintainer, contribution and provenance policy, security contact.

Deferred beyond 1.0: formal-assisted analysis, SVA lint, source style.
