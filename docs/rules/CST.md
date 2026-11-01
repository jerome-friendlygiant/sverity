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
