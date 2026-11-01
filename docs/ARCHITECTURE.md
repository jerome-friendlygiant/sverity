# SVerity architecture

One elaboration, one model, many rules. This page is the map; the code and tests are the truth.

```text
sverity CLI / embedders ── analyze() ── loadDesign() ── slang (parse, elaborate, driver analysis)
                                            │
                       src/domains: Model (bit-level dependency graph, registers, clock domains, resets, guards)
                                    ConeFinder (register fan-in cones)   ClockRelations (SDC clock relationships)
                                            │
                       src/rules: engine runs every Rule once, records one coverage entry per catalog rule
                                            │
                       Report (JSON / SARIF / console), waivers, exit code
```

## Layout

| Path | Role |
|---|---|
| `include/sverity/` | public API: `Rule`, `registerRule()`, `lint::LintRule` + AST helpers, report/diagnostic/catalog/sdc types, `LibCell` |
| `src/frontend/` | slang driver; `slang_rule_map.cpp` maps slang warning options and error codes to catalog rules |
| `src/domains/` | the shared model: `model.cpp`, `cone.cpp`, `relations.cpp`. Everything CDC/RDC reads comes from here |
| `src/rules/lint/` | LNT: `lint_<topic>.cpp` = AST rules on `LintRule`; `slang_backed.cpp` = rules whose findings are slang's; `graph_lint.cpp` = model-based |
| `src/rules/cdc/`, `src/rules/rdc/` | one shared analysis object per family (`Shared`, `RdcShared`) feeding many `PassRule`/`RdcRule` objects; `*_protocol.cpp`/`*_structure.cpp` hold the structure analyses |
| `src/rules/style`, `cst` | STY and CST rules |
| `tests/fixtures/<RULE-ID>/` | annotated SystemVerilog (format at the top of `tests/unit/fixture_runner.cpp`) |
| `tests/unit/` | unit tests (catalog/doc sync, report contract, CLI, SDC, waivers, extension API) |

## Adding a rule

1. Add the entry to `docs/rules/catalog.json` and its row to the family table (`docs/rules/<FAMILY>.md`; titles must match).
2. Implement it: an AST rule derives from `lint::LintRule` (one class + one `ASTVisitor`, see `lint_procedural.cpp`); a model-based rule is
   a `Rule` fed by the family's shared analysis. Pass a `gap` string only if the title is not fully covered. Register it in the family factory.
   Embedders use `registerRule()` instead (a rule outside the catalog is listed as `custom`).
3. Specify it in the family doc: trigger, exemptions, severity, unknown handling. Add fixtures: good, bad, boundary, hierarchy/unknown, and for
   CDC/RDC a mutation (`// revert:` annotations). Unknown or unsupported is never safe: warn and mark the scope INCOMPLETE.
4. Make `catalog.json` agree with the binary: `status: implemented` iff the rule reports EXECUTED; a Partial rule needs `gap`
   (`catalog_sync_with_implemented_rules` fails otherwise; `sverity --list-rules` shows the state).

## Conventions

- Findings are only created through `Lint::report` / `Diagnostic`; severity policy and confidence are part of the rule spec, not the code comments.
- Shared helpers live in one place: AST helpers in `include/sverity/lint.h`, graph helpers on `Model` (`aliasRoot`, `flopBehind`, `traceRoot`), cones in `ConeFinder`.
- Technology knowledge is data, not code: `--library cells.json` declares synchroniser cells (`LibCell`); clock-gate/RAM kinds and profile bundles are future work.
- Tests: a fixture directory name must be a catalog rule ID, and every implemented rule needs at least one fixture (unit-tested).
