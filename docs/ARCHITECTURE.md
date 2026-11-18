# SVerity architecture

One elaboration, one model, many rules. This page records the design decisions; the code and tests are the truth for everything else. What each analysis establishes and assumes is in [`rules/ANALYSIS.md`](rules/ANALYSIS.md).

```text
CLI / embedders ── analyze() ── loadDesign() ── slang (parse, elaborate, driver analysis)
                                     │
          src/analysis   Flow: flow-sensitive analysis of one procedure      GuardMap: path conditions
          src/domains    Model: bit-level dependency graph, registers, clock domains, resets, library cells
                         ConeFinder: register fan-in cones      ClockRelations: SDC clock relationships
                                     │
          src/rules      every Rule runs once; one coverage entry per catalog rule
                                     │
                         Report (JSON / SARIF / console), waivers, exit code
```

| Path | Role |
|---|---|
| `include/sverity/` | public API: `Rule`, `registerRule()`, `lint::LintRule` and the shared AST helpers, report/diagnostic/catalog/sdc types, `LibCell` |
| `src/frontend/` | slang driver; `slang_rule_map.cpp` maps slang warning options and error codes to catalog rules |
| `src/analysis/`, `src/domains/` | the two shared analyses above |
| `src/rules/{lint,cdc,rdc,cst}/` | rules; CDC and RDC each share one analysis object across many rule objects |
| `tests/fixtures/<RULE-ID>/` | annotated SystemVerilog, format at the top of `tests/unit/fixture_runner.cpp` |
| `tools/dev/` | `check.sh` (the one verification command), `conformance.py` (how far each rule and deck is from `validated`), `capacity.py` (OpenTitan capacity runs) |

## Design decisions

1. **Rules are queries on shared analyses.** A rule never walks statements or edges with a private loop when `Flow`, `Model`, `ConeFinder` or an AST helper in `lint.h` answers the question. Foundations are fixed once; rules are re-expressed on them.
2. **One rule = one predicate, one severity policy, one unknown outcome.** A bundled title is split, two rules with the same predicate are merged (the old ID is retired, never reused). The title says exactly what is checked; `catalog.json` marks a rule `implemented` only if the whole title is covered.
3. **Unknown is never safe.** Where an analysis approximates it says so: `Flow` marks a procedure `opaque` (user call, mid-body timing control), `approximated` (a `continue`, a case whose items cannot match) or `bad`; rules then report Unresolved instead of "complete". Model precision loss is a recorded `Fallback`; an unrelated or unconstrained clock relationship stays `Unknown`.
4. **Flow tracks reaching writes.** A nonblocking write changes the scheduled value while later reads still see the current value. Only an exact covering write kills an earlier definition. Proofs that depend on loop iterations or branch exclusivity follow the limits in [ANALYSIS](rules/ANALYSIS.md).
5. **Model facts, not conclusions.** Edges carry type, exactness, control-vs-data, the innermost operator, polarity and branch; registers carry per-bit reset values; constant drivers keep X/Z. Rules decide what a fact means. Operators are classified only when unambiguous (a nested AND/OR mix is "other").
6. **Technology is data.** `--library` declares cells by kind (`synchronizer`, `flop`, `clock_gate`, `ram`, `blackbox`) with port roles and per-port clocks; ASIC and FPGA share the kinds. An undeclared cell is an unresolved black box.
7. **Obligations are explicit and never INFO.** A property RTL cannot show (protocol correctness, recovery/removal, reset release order) is reported as an obligation with proof state `NOT_ATTEMPTED` and is at least a WARNING; `Report::add` enforces this, so INFO + `NOT_ATTEMPTED` survives only for a recognised structure that states its trust model (a 2-flop synchroniser, a library cell, exclusive clocks declared by constraint).
8. **Severity policy is part of the rule spec** (`docs/rules/<FAMILY>.md`), not of the code. Findings are created only through `Lint::report` / `Diagnostic`.
9. **Memory is a design constraint.** A large design has tens of millions of edges, so an edge is 16 bytes and adjacency is one contiguous block. An assignment where every source reaches every target goes through a hub node with no symbol; rules see it as logic and it matches no design object. It changes how the dependency is stored, never which dependency exists. Fan-in cones are computed once, shared across rule families and stored once when equal.
10. **One crossing, one finding, every decision kept.** Several rules can describe one crossing. The most specific enabled one reports it and the others become evidence, but each keeps its own severity override and waiver, so accepting one description does not accept the others.
11. **Measurements stay out of the report.** Reports are deterministic; `--timings` writes time and memory to its own file.
12. **Decks live in the catalog.** Each rule names its earliest deck; the `lint` workflow deck is the rules with a defect class ([lint workflow](rules/LINT_WORKFLOW.md)). `--deck` changes what a run claims, never what it runs or reports.
13. **Provenance.** `Flow` derives from slang's `AbstractFlowAnalysis` (MIT); the interval-map reaching-definition idea follows slang's `DataFlowAnalysis` and slang-netlist's `ValueTracker` (MIT, J. Hanlon). Rule implementations are original; reference projects and separately licensed accuracy-case adaptations are recorded in the crosswalks of `docs/rules/*.md`.

## Rule and catalog ownership

| Source | Owns |
|---|---|
| [Lint spec](rules/LNT.md) | Titles, predicates, exemptions, severity and unknown outcomes. Titles are embedded for offline CLI help and reports. |
| [Catalog](rules/catalog.json) | IDs, status, gaps, decks, defect classes, declared obligation IDs and review metadata. |
| [Analysis](rules/ANALYSIS.md) | Shared proof assumptions and precision limits. |

Change the implementation, spec, catalog and fixtures together. Follow the [lint maintenance workflow](rules/LINT_WORKFLOW.md#changing-checks) for lint changes.
