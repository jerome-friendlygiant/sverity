# STY - Coding convention, source style and formatting diagnostics

One table row per catalogued rule (titles must match `catalog.json`; a unit test checks this). Add a section
per rule below the table when it is specified: trigger, exemptions, severity policy, evidence, tests.

| ID | Required check |
|---|---|
| `STY-001` | Configurable module/package/interface/class/type naming |
| `STY-002` | Configurable signal/port/register/parameter naming |
| `STY-003` | Clock/reset/synchronizer naming and polarity annotation |
| `STY-004` | Explicit versus wildcard port connections policy |
| `STY-005` | Named versus positional parameter overrides |
| `STY-006` | Explicit `default_nettype none` and implicit-net policy |
| `STY-007` | `always_comb`/`always_ff`/`always_latch` preferred coding style |
| `STY-008` | Single purpose procedural block policy |
| `STY-009` | Explicit `begin`/`end` and labeled block consistency |
| `STY-010` | Generate block and instance naming |
| `STY-011` | Line length, indentation, trailing whitespace, tabs |
| `STY-012` | Module/endmodule and block label consistency |
| `STY-013` | Literal sizing, radix, and capitalization style |
| `STY-014` | Comment/header/documentation policy |
| `STY-015` | Package import policy and namespace clarity |
| `STY-016` | Case/default layout, one statement per line, line breaks |
