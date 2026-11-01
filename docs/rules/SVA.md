# SVA - Static lint of SystemVerilog assertions (no assertion proof claim)

One table row per catalogued rule (titles must match `catalog.json`; a unit test checks this). Add a section
per rule below the table when it is specified: trigger, exemptions, severity policy, evidence, tests.

| ID | Required check |
|---|---|
| `SVA-001` | Assertion and property elaboration diagnostics |
| `SVA-002` | Assertion clocking event ambiguity |
| `SVA-003` | Assertion disable condition/reset-domain consistency |
| `SVA-004` | Statically vacuous or unreachable antecedent |
| `SVA-005` | Overlapping and non-overlapping implication misuse patterns |
| `SVA-006` | Suspicious unbounded repetition or delay in assertions |
| `SVA-007` | Disabled/never activated assertion inventory |
| `SVA-008` | Assertion bind scope and referenced object validity |
| `SVA-009` | Unsampled versus sampled signal use in assertions |
| `SVA-010` | Clock-domain/property mismatch in concurrent assertions |
