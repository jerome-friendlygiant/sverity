# SVerity — SystemVerilog Static Analysis RFC

**Project:** SVerity (`sverity`) · **Status:** specification; implementation state is `sverity --list-rules`; open work and research are in the [roadmap](ROADMAP.md)  
**Mission:** an open-source, vendor-independent, self-contained, high-performance SystemVerilog analyzer for lint, CDC, RDC and
progressively stronger correctness analysis; long term, a community-maintained industry-standard tool.  
**Scope of 1.0:** lint (`LNT`), CDC, RDC and the constraint/completeness checks they depend on (`CST`). Style (`STY`) and SVA lint (`SVA`) are outside
this scope and their IDs are retired (section 9).

Rule inventory: [`docs/rules/catalog.json`](rules/catalog.json) (machine-readable; the sign-off / release list: authoritative for IDs and
per-rule `status`) and the per-family tables [`docs/rules/*.md`](rules/) (human-readable titles and, as rules get specified, their
trigger/exemption/severity text). A unit test keeps the two in sync.

## 1. Product charter

- A **native, offline C++20 CLI and embeddable library** using **slang** as the sole mandatory SystemVerilog frontend. No
  Python (runtime), second parser, simulator, formal solver, online service or commercial licence at runtime. (Building
  slang needs Python 3 for its code generator.)
- **Complete lint, CDC and RDC coverage of each release's published frozen deck** (section 8): structural and synthesizability lint, source/destination
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

- **No silent filtering.** JSON and SARIF retain every finding, including INFO and waived items; only exact duplicates are consolidated in the stored report, with occurrences and counts preserved. An optional console view may group related findings by a conservative root-cause identity only when a complete JSON or SARIF report was written. It announces grouping and reports worst severity, severity mix and waived counts. Console truncation is also announced and requires a complete report.
- **Finding identity:** a fingerprint identifies one source finding after exact-duplicate occurrence grouping,
  not its explanation or severity. Its versioned identity comprises rule ID, canonical relative primary file,
  line and column, origin/upstream code, producer-owned semantic discriminator and upstream argument values,
  ordered affected signals, design unit/procedure ordinal when supplied, and structural evidence kinds and
  source positions. A crossing's clock domains, edges and relationship are semantic facts in that identity, so
  changing one stales its waiver. Evidence prose, headline, advice, severity, disposition and rendering options are excluded.
  A producer must supply a discriminator for distinct facts at the same source position (for example an exact
  input bit and destination clock domain); upstream slang findings use diagnostic code and argument values.
  Hierarchy is excluded because identical module-body findings retain one fingerprint and their existing
  occurrence list/count. Line and column are retained conservatively: separate checks can otherwise have
  identical facts. Moving a finding can therefore stale its waiver; arbitrary edit-stability is not promised.
  Distinct findings must not share a fingerprint, or one waiver could cover both. If they would (for example
  one line under two parameter values), those findings alone also hash their instances, then their message
  and evidence text.
  Source identity uses the shortest distinguishing source-path suffix among loaded files (registered independently of rule selection), independently of
  the display root and checkout prefix. Exact duplicate grouping still compares the
  original diagnostic fields, including wording; changing identity must not merge previously separate records.
  Root-cause grouping uses the same semantic identity, retaining its hierarchy boundary and the existing
  LNT-025 procedure/target exception.
- **Fingerprint migration (pre-1.0):** the wording-independent identity replaces the earlier message hash once.
  Existing fingerprint-only waivers become stale and are disclosed by CST-022; regenerate and review them.
  There is no compatibility mode. The report shape is unchanged, so the schema remains `sverity-report/1`.
- **Severity overrides:** rule patterns select effective severities, with original and requested severity retained in JSON and SARIF. Unresolved findings, `UNKNOWN` proofs and `NOT_ATTEMPTED` obligations have a WARNING floor; FATAL, slang errors and invalid-input errors cannot be downgraded. A blocked downgrade is disclosed. An unwaived ERROR downgraded below ERROR remains REVIEW_REQUIRED: strict exit sets bit 2; the `violations` policy gates only effective ERROR/FATAL and execution errors. Coverage and fingerprints are unchanged by overrides.
- **Waiver ≠ deletion.** A waived finding keeps severity, evidence and identity and is shown as `WAIVED`. A waiver needs an exact
  rule ID, a reason, an owner and at least one non-wildcard narrowing field (fingerprint, file, hierarchy or signal), may
  expire, matches per occurrence, and is rejected (with a warning, finding stays open) when expired, stale, overbroad, malformed
  or duplicated (every duplicate ID or identical scope is rejected). Waivers never turn uncertainty into SAFE or alter analysis coverage.
  FATAL findings and slang errors cannot be waived.

  **In-source form:** `// sverity-waive CDC-003 owner="team" reason="held stable by protocol" expires="2027-01-01"`.
  Only line comments beginning with `sverity-waive` after optional whitespace are directives. An exact rule ID is followed
  by whitespace-separated `key="value"` attributes; `owner` and `reason` are required, `expires`, `signal` and `hierarchy`
  optional. Quoted values use backslash escaping. Unknown or repeated attributes and block-comment forms are malformed.
  A standalone directive targets the next lexical token's physical line in the same file, skipping blank/comment-only lines;
  a trailing directive targets its own physical line. It never extends across a statement or procedure. File and line are
  exact, with optional signal/hierarchy filters narrowing further. CDC-003 and CDC-028 locate findings at the procedure:
  place their directive before/on that header, optionally narrowing by the reported signal. An assignment-line directive
  that matches nothing warns as stale; it does not reach back to the header. Source locations appear with applied waivers.
  Slang's lexer reads already loaded source-manager buffers, including includes and loaded `-v`/`-y` units, deduplicating
  repeated includes by physical comment location. Comments in inactive branches are examined too and warn if stale.
  Unloaded library candidates are outside the run and are not read; only loaded files can supply source waivers.
  `--no-source-waivers` disables this scan and is recorded as `inputs.source_waivers=false`.
  `--waiver-template <file>` writes every open/review-required finding as a JSON waiver using its fingerprint and location,
  including unproved findings, with a reminder that waiving does not prove them. Both owner and reason default to
  `<REQUIRED>`, which the common validator rejects until replaced by nonblank human values. Feeding the template back
  unchanged warns for every entry and waives nothing.
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

`--jsonl <file|->` writes finalized, deterministic UTF-8 JSON objects, one per line, under
`sverity-events/1` (`schema_version: "1.0.0"`), independently of `sverity-report/1`.
Every record has a contiguous one-based `sequence` and `type`: `run_start` carries tool and inputs;
`finding` carries the fingerprint and complete canonical `diagnostic` (including occurrence counts);
`coverage` carries complete canonical coverage; `waiver` carries each canonical waiver record.
The final `run_end` carries the canonical summary, selected exit policy/code, finding-record and
occurrence totals, total record count including itself, `complete:true` and `truncated:false`.
EOF without that completion record means an incomplete transport, regardless of analysis coverage.
Earlier report-write failures are reflected in completion execution status and exit code; failure writing
JSONL itself cannot promise a completion record. Human output goes to stderr when JSON, JSONL or SARIF
uses stdout; only one machine writer may use stdout. Terminal limits never apply to these records.

Shared register-to-register CDC findings optionally carry `crossing`: `launch` and `capture` each contain
`signal`, `clock_domain`, `sdc_clock`, `edge` and a procedure `location`; the crossing also contains
`relationship` (`SAME`, `ASYNC`, `EXCLUSIVE`, `UNKNOWN`), `width_bits` and `logic_on_path`.
Endpoints identify the representative registers of the signal-level group, not a path witness or an
exhaustive list of bit locations. Edges are `posedge`, `negedge`, `both` or `unknown`; an unconstrained
SDC name is empty and an unavailable location is `{}`. The optional field is additive within
`sverity-report/1`. Launch procedure positions participate in structural evidence identity under the
existing source-position contract; adding them once before release can stale fingerprint waivers.
Its other presentation fields add no identity inputs.

**Formal-proof extension contract.** A check may return `PROVEN`, `COUNTEREXAMPLE`, `UNKNOWN` or `NOT_ATTEMPTED`, recording
assumptions, engine/version, bounded vs unbounded, timeout and witness. `UNKNOWN`/timeout is a WARNING; bounded success or an
unattempted proof is never reported as unbounded proof.

## 4. Rules

The catalog is an **auditable, growing, versioned inventory**, not a claim of universal exhaustiveness. Every catalogued rule is
native, specified, tested and tracked; an incomplete rule stays visible.

| Family | Scope |
|---|---|
| `LNT` | Semantic, structural and synthesizability lint |
| `STY` | Outside 1.0 scope (formatter territory); IDs retired |
| `SVA` | Outside 1.0 scope (needs a property engine); IDs retired |
| `CDC` | Clock-domain crossings, synchroniser/protocol structure |
| `RDC` | Reset-domain crossings, async reset behaviour and coordination |
| `CST` | Constraints, analysis completeness, trust models, report integrity |

Selected case-analysis modes use CST-014 WARNING Unresolved to disclose unanalysed alternatives and counts of pruned syntax. They never establish multi-mode safety; clock/reset inactivity and internal-target forcing remain unsupported.

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

**Constraints.** The SDC reader interprets clock declarations, clock groups, path exceptions and input delays,
with scalar `set` (assignment and read), single-pass `$var` / `${var}` substitution in bare and quoted words,
and one-variable, one-list `foreach` with a literal braced body. Braces suppress substitution except
backslash-newline. Loops retain their variable afterwards; body diagnostics retain their source lines.
`source` resolves paths relative to the sourcing file and shares scalar variables across nested files.
Each sourced command is rejected independently, like inline text; cycles, unreadable files, excessive
depth and source options are disclosed. File provenance is relative to the report root.
`all_inputs` denotes every top-level input, including clock ports, resolved against the elaborated design
where port references are matched. Options and clock-only uses are disclosed and reject the command.
`set_output_delay -clock X delay ports` supplies the CST-012 output-boundary contract:
launch registers are traced through the combinational cone and compared to external capture clock X.
A consistent contract is INFO with physical timing explicitly unproved; declared asynchronous launches
are ERROR; unknown relationships and incomplete or mixed cones remain Unresolved and INCOMPLETE.
Outputs without delays retain existing behavior. Output delays require output ports and a declared clock;
they do not feed input-only CDC-029. `set_case_analysis 0|1 objects` applies atomically per declaration to
scalar top-level data inputs: the model is rebuilt with consistent mode constants, the report inputs name the
applied values and CST-014 discloses unanalysed alternatives, retaining INCOMPLETE coverage. Internal, driven and
multi-bit targets and clock or reset event sources stay unapplied (CST-009) with a reason; conflicting values apply
nothing. Every object is checked; unresolved objects and invalid case values produce CST-018. Bare names mean top-level ports.
Commands with syntax, substitution or argument problems apply no constraint. A failed loop applies none
of its constraints. Unsupported commands are disclosed through CST-009 and parse/evaluation failures through
CST-018; dependent values are never guessed. Relationships without valid declarations remain **unknown**.
This reads declarations, not a timing proof or verification of the user's clock intent.

The bounded evaluator does not implement general Tcl: `expr`, `if`, `proc`, `lindex`, multiple-variable/list
loops, array/namespace variable substitution, embedded command substitutions and nontrivial backslash
escapes are disclosed. `foreach` allows 1000 iterations per loop and 10000 per parse, with nesting bounded
at 64; exceeding a bound rejects the loop. Unsupported script commands invalidate remembered variables,
so later constraints cannot silently reuse values that an unevaluated command might have changed.
No SGDC, UPF or SDF input.

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
  checks on every fixture. This is the required shape for `validated`; not every implemented rule has all of it yet (see the [roadmap](ROADMAP.md)). Fixtures are annotated SystemVerilog files (see the runner), so adding a case needs no C++. Code map: `docs/ARCHITECTURE.md`.
- **Mutation tests are mandatory** for CDC/RDC: bypass a synchroniser stage, add logic between stages, fan out the first stage,
  synchronise bus bits independently, break Gray pointers, change constraints, flip reset polarity, break reset release, introduce
  a partial drive, violate sizing/case rules. Each injected defect has expected findings, and reverting it clears them (fixtures carry `// revert: <old> => <new>` and `// revert-expect-none:` annotations; the runner analyses the reverted variant too).
- **Public projects**, pinned by URL and commit, never vendored, never assumed defect-free. Two roles: robustness/performance runs
  (PicoRV32, Ibex, OpenTitan) and a **known-good CDC corpus** (PULP `common_cells` CDC blocks, Forencich `verilog-axis` async FIFO, with the
  constraints they need) on which a correct, declared-asynchronous design must produce no ERROR; every false positive found there becomes a
  fixture. The gate (`tools/dev/check.sh`) requires successful elaboration, a minimum register count, zero FATAL/ERROR findings and the recorded review obligations, while preserving INCOMPLETE and REVIEW_REQUIRED. Passing checks structural results, not physical timing or protocol sign-off. sv-tests is frontend only, not a CDC oracle.
- **Differential evaluation** against slang, Verible, Verilator and documented vendor rules is allowed but never replaces native
  rule-level tests; a commercial tool's output is not automatically ground truth.

## 8. Acceptance criteria for 1.0

1.0 is a large target on purpose: it validates the rules we keep, not a small subset. How we get there is in the [roadmap](ROADMAP.md#2-mid-term-reaching-10).

- **Build:** C++20 + slang; offline reproducible build from pinned sources; no Python or other tools at runtime.
- **Interfaces:** deterministic CLI/API, JSON and SARIF; stable rule IDs and finding identity; every finding visible; ASIC and FPGA profiles; explicit clock, reset and black-box contracts.
- **Frozen deck:** a published list of the rules 1.0 stands behind, covering lint, CDC, RDC and the constraint checks they need. A rule leaves the deck only for a written reason, decided before the freeze. Rules outside it stay available and are labelled preview.
- **Validated rules:** every deck rule is `validated`, with a written trigger, exceptions and severity policy, good, bad, boundary, unknown and mutation tests, and a passed independent adversarial review; no undocumented disabled or TODO rules. A separate AI reviewer counts as independent, and the release says so: it is not a human signoff.
- **Honest coverage:** full clock, reset, constraint and graph coverage accounting; no silent "unknown is safe"; proof obligations stay explicit. A run reports `COMPLETE` for the deck when all its rules ran and nothing limited them. Strict exit 0 is then possible on a design with nothing to review, and only then.
- **Evidence:** curated public-project and mutation suites; the known-good corpus gate passing; false-positive and false-negative investigations written down.
- **Capacity:** the whole OpenTitan Earl Grey SoC (`top_earlgrey`) at a pinned commit analyses to completion, with time, memory and coverage published for stated hardware and compiler versions.
- **Reference crosswalk:** each relevant rule of slang, slang-tidy, Verible, Verilator, AMD and Altera is mapped to a native rule or a justified exclusion, and reviewed.
- **Comparison:** a differential run against a commercial tool if legitimate access exists. Otherwise the release says plainly that no comparison was made.
- **Governance:** rule proposal template, licensing and provenance policy, security disclosure policy.

"Industry standard" is a community adoption goal, not something the project can confer on itself.

## 9. Inherent limits and retired IDs

These are facts about the tool, not open work. Bugs and unfinished work are in the [roadmap](ROADMAP.md).

- **Undecidable from RTL:** RDC-029 needs UPF/CPF. CDC-024, LNT-071, RDC-013 and CST-024 cannot be decided from RTL and only emit unknown or obligation results.
- **Rule sources:** commercial rulesets (STARC, OpenMORE, SpyGlass/VC SpyGlass rule references) are not public and are not used.

Retired (never reused): STY-001 through STY-016 (STY-006 → LNT-075, STY-007 → LNT-076), SVA-001 through SVA-010.

Retired (merged, never reused): LNT-021 → RDC-028, LNT-023 → LNT-003/057, LNT-028 → CST-005, LNT-030 → STY-* (now out of scope), LNT-043 → LNT-025, LNT-046 → LNT-047, LNT-052 (dropped: an inferred enable has no defect semantics),
CDC-011/036 → CDC-010, CDC-021 → RDC-007, CDC-022 → CST-004/CDC-038, CDC-025 → CDC-008, CDC-027 → CDC-007, CDC-033 → CDC-008/018, CDC-042 → CDC-013, RDC-015 → RDC-003/007, RDC-018 → RDC-008, RDC-021 → RDC-026, RDC-022 → RDC-007.
