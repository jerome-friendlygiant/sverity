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
- **Trigger:** each `create_clock` source pattern that matches no design object.
- **Severity:** WARNING Unresolved.
- **Unknown:** `constraints` is marked INCOMPLETE. Other named constraint paths are not checked by this rule.

### CST-004
- **Trigger:** each clock-domain pair with Unknown relationship, and each crossing whose relation is Unknown.
- **Severity:** WARNING Unresolved.
- **Unknown:** relationships remain unknown and `clock-relationships` is INCOMPLETE. Modeling uses SDC clock names and clock groups; generated-clock and exception semantics are not applied.

### CST-005
- **Trigger:** instances of modules reported as unknown by elaboration, grouped by module and reported as unmodeled black boxes.
- **Severity:** WARNING Unresolved.
- **Unknown:** behavior is unanalyzed and `blackboxes` is INCOMPLETE. Blackbox-attributed or encrypted modules not reported as unknown are not distinguished.

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
- **Trigger:** each modeled clock domain without a matching clock constraint.
- **Severity:** WARNING Unresolved.
- **Unknown:** crossings involving that clock remain unknown and `constraints` is INCOMPLETE. Only clock constraint coverage is checked.

### CST-012
- **Trigger:** each top-level output port matched by a valid `set_output_delay -clock X`; trace its cone to launching registers and report the output boundary.
- **Severity:** INFO Established when complete and synchronous to X; ERROR Established for a known asynchronous relation. An incomplete cone or unknown/exclusive relation is WARNING Unresolved; a known async case may retain ERROR with Unresolved confidence.
- **Unknown:** input feed-through, no launch register, mixed/unknown domains, opaque calls, black-box, unresolved or undriven leaves make the cone uncertain and mark `constraints` INCOMPLETE. Physical timing is not proved.

### CST-014
- **Trigger:** an applied binary `set_case_analysis` declaration emits a warning with fixed values and run totals of pruned conditional branches and mux expressions.
- **Severity:** WARNING Unresolved.
- **Unknown:** alternative values and crossings are not analyzed; `constraints` and `case-analysis-modes` are INCOMPLETE. Unapplied declarations are reported under CST-009, with object/conflict problems also under CST-018.

### CST-018
- **Trigger:** constraint parse problems, unresolved referenced objects/clocks, conflicting clock-name definitions, or unreadable/invalid library or SDC files.
- **Severity:** WARNING Unresolved for parsed problems and unresolved/conflicting references; ERROR for unreadable or invalid library files and unreadable SDC files.
- **Unknown:** the affected declaration/file is not fully applied and `constraints` is INCOMPLETE. Semantic option-value validation is limited.

### CST-019
- **Trigger:** catalogued rules are not fully evaluated in a run, or a disabled-rule list names an unknown rule.
- **Severity:** WARNING Unresolved.
- **Unknown:** absence of findings for rules not fully evaluated is not evidence of safety; `analysis_coverage` is INCOMPLETE. Profile policy drift is not checked.

### CST-022
- **Trigger:** waiver expiry is invalid or past, scope has no narrowing criterion, or a waiver matches no finding in this run (including hierarchy scope that cannot safely split incomplete occurrence lists).
- **Severity:** WARNING Unresolved; rejected waivers are not applied.
- **Unknown:** stale, overbroad, or expired waivers do not suppress findings; validation does not mark an analysis scope INCOMPLETE.
