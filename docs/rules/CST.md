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

## CST-012 — Top-level IO and abstract IP CDC contracts

- **Trigger:** a valid `set_output_delay -clock X` matches a top-level output. Trace its combinational cone to launching registers, using the same named-clock relationship contract as CDC-029. Aggregate bits of each output port per declaration.
- **Exemptions:** outputs without output delays produce no finding. A synchroniser before the output is not an exemption: its final register launches into the external capture boundary.
- **Severity:** INFO when every launch register has a resolved clock synchronous to X and the cone has one launch domain and no unknown leaves; state explicitly that physical timing is not proved. A declared asynchronous launch/capture relationship is ERROR, as for CDC-003, even if other cone sources are unresolved.
- **Unknown:** WARNING Unresolved and INCOMPLETE constraints for unknown/exclusive relationships, mixed launch domains, primary-input feed-through, no launching register, opaque calls, black-box leaves, unresolved or undriven paths. A proven asynchronous crossing retains ERROR alongside Unresolved confidence when the cone is also incomplete. Function bodies contribute their state dependencies; function names do not imply safety.
- **Evidence:** the SDC declaration and launch registers with their clock relationships. Delay values are not setup/hold proofs. Generated clocks are usable by their declared name; distinct clock names require an established relationship and are otherwise unknown.

## CST-014 — Dynamic modes and mutually exclusive clock configurations

- **Trigger:** a binary case-analysis declaration is applied to scalar top-level input ports. WARNING Unresolved names the fixed values, states that alternatives are not covered, and gives run totals of pruned conditional branches and mux expressions. Counts describe syntax selections, not removed crossing counts.
- **Exemptions:** unapplied declarations use CST-009; unknown objects and conflicting values also use CST-018.
- **Unknown:** the `case-analysis-modes` and constraints scopes remain INCOMPLETE. Alternative crossings are not inventoried; analyse the opposite values in separate runs. No cross-mode safety is inferred.
- **Evidence:** original declaration and applied values in report inputs; console summary names the selected mode. Internal targets, vectors, internally driven inputs and clock/reset event sources remain unapplied with reasons. Conflicting declarations apply nothing to the conflicting object. Calls prevent condition pruning, including a constant conjunct.
