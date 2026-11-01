# SVerity architecture

One elaboration, one model, many rules. This page records the design decisions; the code and tests are the truth for everything else.

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
| `src/rules/{lint,cdc,rdc,style,cst}/` | rules; CDC and RDC each share one analysis object across many rule objects |
| `tests/fixtures/<RULE-ID>/` | annotated SystemVerilog, format at the top of `tests/unit/fixture_runner.cpp` |

## Design decisions

1. **Rules are queries on shared analyses.** A rule never walks statements or edges with a private loop when `Flow`, `Model`, `ConeFinder` or an AST helper in `lint.h` answers the question. Foundations are fixed once; rules are re-expressed on them.
2. **One rule = one predicate, one severity policy, one unknown outcome.** A bundled title is split, two rules with the same predicate are merged (the old ID is retired, never reused). The title says exactly what is checked; `catalog.json` marks a rule `implemented` only if the whole title is covered.
3. **Unknown is never safe.** Where an analysis approximates it says so: `Flow` marks a procedure `opaque` (user call, mid-body timing control), `approximated` (a `continue`, a case whose items cannot match) or `bad`; rules then report Unresolved instead of "complete". Model precision loss is a recorded `Fallback`; an unrelated or unconstrained clock relationship stays `Unknown`.
4. **Flow semantics.** A nonblocking write changes only the final value, not what later reads see. A write is overwritten ("killed") only by an exact write of the same kind that covers it. Every access under a loop is `inLoop` (loops are walked once), so no overwrite claim crosses iterations. Comparison-based exclusivity of two paths is withdrawn when its operand is written earlier.
5. **Model facts, not conclusions.** Edges carry type, exactness, control-vs-data, the innermost operator, polarity and branch; registers carry per-bit reset values; constant drivers keep X/Z. Rules decide what a fact means. Operators are classified only when unambiguous (a nested AND/OR mix is "other").
6. **Technology is data.** `--library` declares cells by kind (`synchronizer`, `flop`, `clock_gate`, `ram`, `blackbox`) with port roles and per-port clocks; ASIC and FPGA share the kinds. An undeclared cell is an unresolved black box.
7. **Obligations are explicit and never INFO.** A property RTL cannot show (protocol correctness, recovery/removal, reset release order) is reported as an obligation with proof state `NOT_ATTEMPTED` and is at least a WARNING; `Report::add` enforces this, so INFO + `NOT_ATTEMPTED` survives only for a recognised structure that states its trust model (a 2-flop synchroniser, a library cell, exclusive clocks declared by constraint).
8. **Severity policy is part of the rule spec** (`docs/rules/<FAMILY>.md`), not of the code. Findings are created only through `Lint::report` / `Diagnostic`.
9. **Provenance.** `Flow` derives from slang's `AbstractFlowAnalysis` (MIT); the interval-map reaching-definition idea follows slang's `DataFlowAnalysis` and slang-netlist's `ValueTracker` (MIT, J. Hanlon). Nothing is copied; reference projects are recorded in the crosswalks of `docs/rules/*.md`.

Adding a rule: see README "Extending"; the catalog entry, the family-doc row and spec block, the code and the fixtures change together in one commit.
