# SVerity — SystemVerilog Static Analysis RFC

**Project:** SVerity (`sverity`) · **Status:** proposed specification, partially implemented (see `sverity --list-rules`)  
**Mission:** an open-source, vendor-independent, self-contained, high-performance SystemVerilog analyzer for lint, CDC, RDC and
progressively stronger correctness analysis; long term, a community-maintained industry-standard tool.

Rule inventory: [`docs/rules/catalog.json`](rules/catalog.json) (machine-readable; the sign-off / release list: authoritative for IDs and
per-rule `status`) and the per-family tables [`docs/rules/*.md`](rules/) (human-readable titles and, as rules get specified, their
trigger/exemption/severity text). A unit test keeps the two in sync.

## 1. Product charter

- A **native, offline C++20 CLI and embeddable library** using **slang** as the sole mandatory SystemVerilog frontend. No
  Python (runtime), second parser, simulator, formal solver, online service or commercial licence at runtime. (Building
  slang needs Python 3 for its code generator.)
- **Complete in-scope lint, CDC and RDC coverage for every advertised release**: style, structural dataflow, source/destination
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
   Rules: slang-backed · style · graph lint · CDC · RDC · constraints · (SVA) · (proof, future)
        │
   Report: evidence · waivers · coverage · proof obligations → console, JSON, SARIF, exit code
```

One elaboration and one model per run; each check is a rule class or a shared analysis pass, never an independent parse.
Future proof engines consume the same model through explicit interfaces.

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
| `INFO` | Context, classified-safe structures (with their assumptions), statistics, advisory findings. |
| `WARNING` | Potential defect, heuristic, unverified property, incomplete knowledge or unsupported analysis. If safety cannot be determined, at least this. |
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

| Family | Scope | Rules |
|---|---|---:|
| `LNT` | Semantic, structural and synthesizability lint | 61 |
| `STY` | Coding convention, source style and formatting | 16 |
| `SVA` | Static lint of SystemVerilog assertions (no proof claim) | 10 |
| `CDC` | Clock-domain crossings, synchroniser/protocol structure | 44 |
| `RDC` | Reset-domain crossings, async reset behaviour and coordination | 29 |
| `CST` | Constraints, analysis completeness, trust models, report integrity | 24 |

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

Required native coverage includes: style/naming/layout; X/Z, signedness, sizing and synthesis-vs-simulation hazards; FSM and
static semantic analysis with an explicit unknown/proof-obligation distinction; CDC (synchronisers, multi-bit coherency,
reconvergence, pulse/toggle/handshake/FIFO, generated/gated/muxed clocks, constraint validity); RDC (multiple resets,
assertion/release, synchronisers, reconvergence, sequencing, CDC/RDC interaction); static SVA checks with reusable property
representations.

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
| Lint breadth | `LNT`, `STY`, `SVA` | every supported rule individually verified |
| Clock/reset framework | clock extraction, SDC subset, reset modelling, black boxes, completeness | unknown/unresolved cases fail safe |
| CDC + RDC breadth | full structural/protocol recognition and reset-domain checks | good/bad/boundary/constraint/unknown cases per rule |
| Conformance release | frozen baseline deck, public-design and mutation tests | published severity/waiver/review matrix, reproducible benchmarks |
| Formal-assisted | property IR, solver adapter, obligations, counterexamples | reproducible proven/failed/unknown outcomes, no false proof claims |

A milestone may be called *preview* with incomplete rules. A **1.0 "complete to published specification"** claim requires every
frozen rule `validated`, or a transparent scope revision *before* freezing.

## 7. Testing

- **Fixtures per rule** (`tests/fixtures/<RULE-ID>/`): good, bad, boundary, unknown/unconstrained, hierarchy, configuration (the same
  test types for every rule); plus
  report-integrity checks on every fixture. Fixtures are annotated SystemVerilog files (see the runner), so adding a case needs
  no C++.
- **Mutation tests are mandatory** for CDC/RDC: bypass a synchroniser stage, add logic between stages, fan out the first stage,
  synchronise bus bits independently, break Gray pointers, change constraints, flip reset polarity, break reset release, introduce
  a partial drive, violate style/sizing/case rules. Each injected defect has expected findings, and reverting it clears them.
- **Public projects**, pinned by URL and commit, never vendored, never assumed defect-free: PicoRV32, Ibex, OpenTitan, PULP AXI,
  sv-tests (frontend only, not a CDC oracle). Only `picorv32` is wired up so far (`tests/public_projects`).
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

## 9. Open issues in this specification

1. The original catalog listed `good/bad/boundary/configuration/report_evidence` for every rule while §7 lists `unknown`/`hierarchy`; fixtures use the union for all rules. Tailor per rule when specifying it.
2. `LNT-030` duplicates the `STY` family; resolve into concrete `STY-*` rules before 1.0.
3. Overlapping rules need explicit trigger boundaries (e.g. LNT-004/056/057, CDC-004/009/016/032/039, RDC-006/016, CDC-038 / CST-008 / RDC-027).
4. Some rules cannot be decided from RTL alone (CDC-024, LNT-029, RDC-013, CST-024); they may only emit unknown/obligation results.
5. RDC-012, RDC-021 and RDC-029 need power intent (UPF/CPF), which is not a specified input.
6. No rule has a severity policy yet (`default_severity` is null everywhere); native severities are provisional, slang findings keep slang's.
7. The profile file format (ASIC/FPGA cell and primitive libraries) is not yet defined.
8. Not yet audited: scale beyond ~3000 register bits, memories, interfaces, user functions, latches, clang builds.
