# LNT - Semantic, structural and synthesizability lint

One table row per catalogued rule (titles must match `catalog.json`; a unit test checks this). Add a section
per rule below the table when it is specified: trigger, exemptions, severity policy, evidence, tests.

| ID | Required check |
|---|---|
| `LNT-001` | Syntax, preprocess and elaboration failure |
| `LNT-002` | Missing/duplicate module, package and symbol references |
| `LNT-003` | Illegal or missing port connections |
| `LNT-004` | Width mismatch, truncation and unexpected extension |
| `LNT-005` | Signedness mismatch and implicit conversion hazards |
| `LNT-006` | Out-of-range bit/part select or invalid array index |
| `LNT-007` | Implicit net declarations or typos |
| `LNT-008` | Undriven net or undriven bit range |
| `LNT-009` | Multiple or conflicting drivers, including mixed procedural/continuous |
| `LNT-010` | Unconnected input and output ports |
| `LNT-011` | Unused nets, variables, parameters and ports |
| `LNT-012` | Combinational cycles, including cross-module cycles |
| `LNT-013` | Inferred latch / incomplete combinational assignment |
| `LNT-014` | `always_ff` misuse / mixed procedural drivers |
| `LNT-015` | Blocking/nonblocking assignment policy violations |
| `LNT-016` | Unsafe `casex`, broad wildcard or incomplete case policy |
| `LNT-017` | Incomplete FSM case coverage |
| `LNT-018` | Constant condition, unreachable branch or dead assignment |
| `LNT-019` | Non-synthesizable construct in design RTL |
| `LNT-020` | Reset-only assignment or unexpected missing reset |
| `LNT-021` | Inconsistent reset polarity on related registers |
| `LNT-022` | Mixed positive/negative edge or multiple clock controls in one register |
| `LNT-023` | Width/type incompatibility across parameterized interfaces |
| `LNT-024` | Suspicious undriven output, partially assigned struct/array |
| `LNT-025` | Multiple assignments with unintended priority / overwritten write |
| `LNT-026` | Incorrect constant sizing, shift width and unsized literal hazards |
| `LNT-027` | Unexpected combinational cross-coupling of clock/reset/control |
| `LNT-028` | Undeclared, unresolved or unmodeled blackbox boundary |
| `LNT-029` | FSM dead/stuck states requiring reachability proof |
| `LNT-030` | Style/naming/formatting |
| `LNT-031` | Illegal or unsafe assignment to net / variable kind |
| `LNT-032` | Duplicate or overlapping case arms |
| `LNT-033` | Suspicious `unique`, `unique0`, `priority` claims |
| `LNT-034` | Conditional expression width/sign ambiguity |
| `LNT-035` | Unsized literals and signed shifts |
| `LNT-036` | Arithmetic intermediate overflow and truncation |
| `LNT-037` | Divide or modulo by zero where statically provable |
| `LNT-038` | Comparison and logical tautologies / contradictions |
| `LNT-039` | Unintentional X/Z assignments in synthesizable RTL |
| `LNT-040` | X optimism / X pessimism patterns |
| `LNT-041` | Incomplete array, struct, union or interface assignments |
| `LNT-042` | Partial or overlapping output drive through hierarchy |
| `LNT-043` | Overlapping nonblocking updates to one register |
| `LNT-044` | Sequential variable read before intended assignment |
| `LNT-045` | Combinational sensitivity mismatch in legacy `always` |
| `LNT-046` | Infer unintended gated clocks from data logic |
| `LNT-047` | Data-driven asynchronous controls / events |
| `LNT-048` | ASIC versus FPGA initialization assumptions |
| `LNT-049` | Suspicious inferred RAM or mixed memory access semantics |
| `LNT-050` | Input/output port direction or interface modport misuse |
| `LNT-051` | Unreachable generate alternatives for selected configuration |
| `LNT-052` | Unintended inferred clock enable or priority selection |
| `LNT-053` | Unreachable or duplicate assignment overwritten in one path |
| `LNT-054` | Uninitialized state dependency and X-sensitive control |
| `LNT-055` | Suspicious arithmetic saturation/wraparound assumptions |
| `LNT-056` | Constant width mismatch inside concatenation/replication |
| `LNT-057` | Unsafe truncation across implicit parameterized casts |
| `LNT-058` | Incorrect async reset/set event-control coding |
| `LNT-059` | Unused package imports / declarations and shadowing |
| `LNT-060` | Potential combinational race from blocking assignments across processes |
| `LNT-061` | Dependency on non-synthesizable system tasks/functions |
