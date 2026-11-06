# CST - Constraints, analysis completeness, trust models and report integrity

One table row per catalogued rule (titles must match `catalog.json`; a unit test checks this). Add a section
per rule below the table when it is specified: trigger, exemptions, severity policy, evidence, tests.

| ID | Required check |
|---|---|
| `CST-001` | Every register has an identified clock and reset classification (including explicit `no_reset`) |
| `CST-002` | All named constraint paths resolve to actual design objects |
| `CST-003` | No conflicting clock relationships or contradictory exceptions |
| `CST-004` | Crossings with unknown relationship are flagged as `unresolved`, not `safe` |
| `CST-005` | All blackbox instances are inventoried and marked modeled/unmodeled |
| `CST-006` | Waivers have narrow matching criteria, reason, owner and optional expiry |
| `CST-007` | Every recognized synchronizer/CDC primitive has an explicit, documented trust model |
| `CST-008` | Timing-cut and false-path exceptions do not imply CDC safety |
| `CST-009` | SDC subset support and unsupported Tcl commands are disclosed |
| `CST-010` | Unsupported constructs or bit-precision fallbacks are tracked in coverage report |
| `CST-011` | Source-clock constraint coverage of all sequential endpoints |
| `CST-012` | Top-level IO and abstract IP CDC contracts |
| `CST-013` | Generated clock divider/multiplier/phase consistency |
| `CST-014` | Dynamic modes and mutually exclusive clock configurations |
| `CST-015` | Multi-configuration parameter/generate completeness |
| `CST-016` | Reset-state and reset-sequence assumptions are explicit |
| `CST-017` | Blackbox boundary protocol and reset metadata quality |
| `CST-018` | Constraint parser completeness with unsupported directives |
| `CST-019` | Rule/profile enablement coverage and policy drift |
| `CST-020` | Crossing inventory consistency and report completeness |
| `CST-021` | Constraint source provenance and conflict trace |
| `CST-022` | Waiver expiration, stale match and overbroad-waiver warning |
| `CST-023` | Selective blackbox and encrypted RTL coverage reporting |
| `CST-024` | Physical-assumption proof obligation inventory |

### CST-001
- **Trigger:** clock-resolved register bits are summarized by asynchronous reset, synchronous-reset candidate, or no reset; reset roots get INFO summaries. Unidentified-clock bits get a separate warning.
- **Severity:** INFO Informational for reset-root and per-domain classification; WARNING Unresolved for register bits without an identified clock.
- **Unknown:** reset semantics are not proven; unidentified clocks mark `clock-extraction` INCOMPLETE. Explicit user-declared `no_reset` is not handled.

### CST-002
- **Trigger:** clock target patterns or generated-clock `-source` patterns matching no design object; an explicit generated `-master_clock` naming no declared clock.
- **Exempt:** existing source objects and declared master names, including declarations later in the file.
- **Severity:** WARNING Unresolved.
- **Unknown:** `constraints` is marked INCOMPLETE. Invalid derivation never establishes synchrony; distinct clock names remain Unknown unless clock groups explicitly relate them.

### CST-003
- **Trigger:** a same-name clock redeclaration without `-add` changes period, waveform, target, or generated source/transform; or clock groups separate a generated clock from a declared ancestor (including transitive ancestors).
- **Exempt:** identical repeated definitions, a later `-add` declaration, and ancestor/descendant clocks in the same group. Primary default waveform equals explicit `{0 period/2}`. Generated transform options are compared as parsed text, without algebraic equivalence.
- **Severity:** WARNING Established for overrides, located at the later declaration with both declarations as evidence. WARNING Heuristic for ancestry/group contradictions, located at the group with clock declaration evidence.
- **Unknown:** the latest definition owns its name in analysis. Derivation is an SDC assumption, not RTL proof; no synchronous relation is inferred. The first matching clock-group declaration determines Async or Exclusive (and cuts STA timing); absent groups, distinct names remain Unknown. Ancestry traversal stops on cycles and retains all candidate masters when a source object carries multiple clocks. A `-master_clock` pattern selects every declared clock it matches, and a `-source` pin reaches a clock declared on the port connected to it.

### CST-004
- **Trigger:** each clock-domain pair with Unknown relationship, and each crossing whose relation is Unknown.
- **Severity:** WARNING Unresolved.
- **Unknown:** relationships remain unknown and `clock-relationships` is INCOMPLETE. Modeling uses SDC clock names and clock groups; generated-clock and exception semantics are not applied.

### CST-005
- **Trigger:** instances of unmodeled black boxes, grouped by module: modules reported as unknown by elaboration, --library cells of kind blackbox, and modules attributed `black_box`, `blackbox` or `syn_black_box` (not set to 0), including the analysed top itself, whose body is then not analysed at all. An attributed module's body is a stand-in that synthesis replaces, so it is not walked; its outputs are black-box driven, with the directions its ports declare.
- **Exempt:** an attribute set to 0.
- **Severity:** WARNING Unresolved.
- **Unknown:** behavior is unanalyzed and `blackboxes` is INCOMPLETE. Encrypted modules are not distinguished.

### CST-006
- **Trigger:** malformed or duplicate waivers, or waivers with unknown/non-exact rule IDs or blank/placeholder reason or owner.
- **Severity:** WARNING Unresolved; rejected waivers are not applied.
- **Unknown:** invalid waiver data cannot authorize suppression; validation does not mark an analysis scope INCOMPLETE.

### CST-009
- **Trigger:** unsupported SDC commands or parsed declarations recorded but not applied.
- **Severity:** WARNING Unresolved.
- **Unknown:** expressed constraints are not applied and `constraints` is INCOMPLETE. This reports known unsupported/unapplied commands, not general Tcl completeness.

### CST-010
- **Trigger:** precision fallbacks recorded by the connectivity/clock/reset model, grouped by category.
- **Severity:** WARNING Unresolved.
- **Unknown:** results involving a fallback are inconclusive and `graph-precision` is INCOMPLETE. Constructs ignored without a recorded fallback are not enumerated.

### CST-011
- **Trigger:** (a) each modeled clock domain without a matching clock constraint; (b) when a constraint file is supplied, each top-level input captured by a register (as data, not as a clock or asynchronous reset) that no `set_input_delay` names; one per input, at its first capturing procedure.
- **Severity:** WARNING Unresolved.
- **Unknown:** (a) crossings involving that clock remain unknown; (b) the input is analysed as asynchronous to every clock. Either marks `constraints` INCOMPLETE.

### CST-012
- **Trigger:** each top-level output port matched by a valid `set_output_delay -clock X`; trace its cone to launching registers and report the output boundary.
- **Severity:** INFO Established when complete and synchronous to X; ERROR Established for a known asynchronous relation. An incomplete cone or unknown/exclusive relation is WARNING Unresolved; a known async case may retain ERROR with Unresolved confidence.
- **Unknown:** input feed-through, no launch register, mixed/unknown domains, opaque calls, black-box, unresolved or undriven leaves make the cone uncertain and mark `constraints` INCOMPLETE. Physical timing is not proved.

### CST-014
- **Trigger:** an applied binary `set_case_analysis` declaration emits a warning with fixed values and run totals of pruned conditional branches and mux expressions.
- **Severity:** WARNING Unresolved.
- **Unknown:** alternative values and crossings are not analyzed; `constraints` and `case-analysis-modes` are INCOMPLETE. Unapplied declarations are reported under CST-009, with object/conflict problems also under CST-018.

### CST-018
- **Trigger:** constraint parse problems, unresolved referenced objects/clocks in other declarations, or unreadable/invalid library or SDC files.
- **Severity:** WARNING Unresolved for parsed problems and unresolved references; ERROR for unreadable or invalid library files and unreadable SDC files.
- **Unknown:** the affected declaration/file is not fully applied and `constraints` is INCOMPLETE. Semantic option-value validation is limited.

### CST-019
- **Trigger:** catalogued rules are not fully evaluated in a run, or a disabled-rule list names an unknown rule.
- **Severity:** WARNING Unresolved.
- **Unknown:** absence of findings for rules not fully evaluated is not evidence of safety; `analysis_coverage` is INCOMPLETE. Profile policy drift is not checked.

### CST-022
- **Trigger:** waiver expiry is invalid or past, scope has no narrowing criterion, or a waiver matches no finding in this run (including hierarchy scope that cannot safely split incomplete occurrence lists).
- **Severity:** WARNING Unresolved; rejected waivers are not applied.
- **Unknown:** stale, overbroad, or expired waivers do not suppress findings; validation does not mark an analysis scope INCOMPLETE.


### Accuracy-case adaptations

Reduced and modified RTL under `tests/accuracy/cases/CST/` exercises clock dividers and muxes, synchronous interfaces, FIFO boundaries and clock monitoring. The upstream licences apply to these adaptations; each case retains its attribution and records the exact source path and changes in `label.json`. Whole upstream designs are not vendored. Licence texts and notices ship in [tests/accuracy/LICENSE](../../tests/accuracy/LICENSE).

| Source at pinned commit | Licence | Material taken |
|---|---|---|
| [pulp-platform/common_cells](https://github.com/pulp-platform/common_cells/tree/e73baaec2ca665cd80c3c384e9258e35242b829c) | SHL-0.51 | Reduced RTL fragments used as CST test inputs and mutated lookalikes |
| [lowRISC/ibex](https://github.com/lowRISC/ibex/tree/2373e5dadcfc4f56672962fa242f6a051acc9aca) | Apache-2.0 | Reduced RTL fragments used as CST test inputs and mutated lookalikes |
| [lowRISC/opentitan](https://github.com/lowRISC/opentitan/tree/92f3d03b0dcf6d5c1933a65d1497747f67d19f4c) | Apache-2.0 | Reduced RTL fragments used as CST test inputs and mutated lookalikes |
| [alexforencich/verilog-axis](https://github.com/alexforencich/verilog-axis/tree/48ff7a7e2ef782cf778d47910cf85835c64b1bce) | MIT | Reduced RTL fragments used as CST test inputs and mutated lookalikes |
