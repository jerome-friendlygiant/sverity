# CST - Constraints, analysis completeness, trust models and report integrity

One table row per catalogued rule (titles must match `catalog.json`; a unit test checks this). Add a section
per rule below the table when it is specified: trigger, exemptions, severity policy, evidence, tests.

| ID | Required check |
|---|---|
| `CST-001` | Model-derived register clock and reset classification |
| `CST-002` | Clock source, generated-clock master/source, and non-clock path-exception object resolution |
| `CST-003` | Clock redeclaration overrides, asynchronous root aliases, and generated-clock group conflicts |
| `CST-004` | Crossings and primary-input captures with unknown clock relationship flagged as unresolved |
| `CST-005` | Unmodeled black-box instance inventory |
| `CST-006` | Waivers are well-formed with exact catalog rule ID, non-placeholder owner, and reason |
| `CST-007` | Every recognized synchronizer/CDC primitive has an explicit, documented trust model |
| `CST-008` | Timing-cut and false-path exceptions do not imply CDC safety |
| `CST-009` | SDC subset support and unsupported Tcl commands are disclosed |
| `CST-010` | Model connectivity, clock, reset, and pragma precision fallbacks disclosed in coverage |
| `CST-011` | Clock constraint and captured data-input delay coverage |
| `CST-012` | Top-level output-boundary CDC contracts and uncontracted CDC-relevant outputs |
| `CST-013` | Generated clock source and bounded RTL derivation consistency |
| `CST-014` | Applied case-analysis fixed mode values and unanalysed alternatives disclosure |
| `CST-015` | Multi-configuration parameter/generate completeness |
| `CST-016` | Declared reset-state and reset-sequence assumptions are bound, reported and, where the RTL can show it, structurally checked |
| `CST-017` | Blackbox boundary protocol and reset metadata quality |
| `CST-018` | Constraint parser problems, unreadable files, and invalid delay or reference declarations |
| `CST-019` | Rule execution and enablement accounting for catalogued checks |
| `CST-020` | Crossing inventory consistency and report completeness |
| `CST-021` | Constraint source provenance and conflict trace |
| `CST-022` | Waiver expiration, stale match and overbroad-waiver warning |
| `CST-023` | Selective blackbox and encrypted RTL coverage reporting |
| `CST-024` | Physical-assumption proof obligation inventory |

### CST-001
- **Trigger:** clock-resolved register bits are summarized per domain by asynchronous reset, synchronous-reset candidate, or no reset (`RDC-001` separately emits reset-root INFO summaries when enabled under `--goal rdc`). Unidentified-clock bits get a separate warning.
- **Severity:** INFO Heuristic for per-domain classification; WARNING Unresolved for register bits without an identified clock.
- **Unknown:** reset semantics are not proven; unidentified clocks mark `clock-extraction` INCOMPLETE. Explicit user-declared `no_reset` is not handled.

### CST-002
- **Trigger:** clock target patterns, generated-clock `-source` patterns, or non-clock path-exception object selectors (`-from`, `-to`, `-through`) matching no design object; an explicit generated `-master_clock` naming no declared clock. `get_cells` resolves against elaborated sub-instance paths and modeled sequential register cells (never plain combinational nets, non-register top-level ports, or non-instance generate scopes).
- **Exempt:** existing source objects (including matching `get_cells` sub-instances or modeled registers), declared master names (including declarations later in the file), and clock/regexp/all-port selectors checked by their own predicates.
- **Severity:** WARNING Unresolved.
- **Unknown:** `constraints` is marked INCOMPLETE. Invalid derivation never establishes synchrony; distinct clock names remain Unknown unless clock groups explicitly relate them.

### CST-003
- **Trigger:** a same-name clock redeclaration without `-add` changes period, waveform, target, or generated source/transform; a single RTL clock root carries multiple active SDC clocks declared asynchronous by `set_clock_groups`; or clock groups separate a generated clock from a declared ancestor (including transitive ancestors).
- **Exempt:** identical repeated definitions, a later `-add` declaration whose clocks are not declared asynchronous on the same root, and ancestor/descendant clocks in the same group. Primary default waveform equals explicit `{0 period/2}`. Generated transform options are compared as parsed text, without algebraic equivalence.
- **Severity:** WARNING Established for overrides, located at the later declaration with both declarations as evidence. WARNING Unresolved for asynchronous SDC clock aliases on one RTL root (marking `constraints` and `clock-relationships` INCOMPLETE). WARNING Heuristic for ancestry/group contradictions, located at the group with clock declaration evidence.
- **Unknown:** the latest definition owns its name in analysis. Derivation is an SDC assumption, not RTL proof; no synchronous relation is inferred. The first matching clock-group declaration determines Async or Exclusive (and cuts STA timing); absent groups, distinct names remain Unknown. Ancestry traversal stops on cycles and retains all candidate masters when a source object carries multiple clocks. A `-master_clock` pattern selects every declared clock it matches, and a `-source` pin reaches a clock declared on the port connected to it.

### CST-004
- **Trigger:** each observed clock-domain crossing with an Unknown relationship. This includes primary-input captures whose launch relationship under supplied `set_input_delay` intent is Unknown, mixes an Unknown or rejected mode with another mode, or disagrees across Same and Exclusive modes. `set_output_delay` boundary contracts are reported by `CST-012`, not `CST-004`.
- **Severity:** WARNING Unresolved.
- **Unknown:** relationships remain unknown and `clock-relationships` is INCOMPLETE. Modeling uses SDC clock names and clock groups; generated-clock and exception semantics are not applied.

### CST-005
- **Trigger:** instances of unmodeled black boxes, grouped by module: modules reported as unknown by elaboration, --library cells of kind blackbox, and modules attributed `black_box`, `blackbox` or `syn_black_box` (not set to 0), including the analysed top itself, whose body is then not analysed at all. Report the total count and provide up to eight instance sites per module; `signals` lists the nets connected to output-capable ports and the registers/top-level outputs they drive directly. An attributed module's body is a stand-in that synthesis replaces, so it is not walked; its outputs are black-box driven, with the directions its ports declare.
- **Exempt:** an attribute set to 0.
- **Severity:** WARNING Unresolved.
- **Unknown:** behavior is unanalyzed and `blackboxes` is INCOMPLETE. Encrypted modules are not distinguished.

### CST-006
- **Trigger:** unreadable or syntactically invalid waiver files, malformed or duplicate waivers, or waivers with unknown/non-exact rule IDs or blank/placeholder reason or owner.
- **Severity:** ERROR Established (with execution ERROR) when a waiver file cannot be read or parsed; WARNING Unresolved for malformed, duplicate, or invalid waiver entries. Rejected waivers are not applied.
- **Unknown:** invalid waiver data cannot authorize suppression; entry-level validation does not mark an analysis scope INCOMPLETE, while an unreadable or unparseable waiver file sets execution ERROR and INCOMPLETE coverage.

### CST-009
- **Trigger:** unsupported SDC commands or parsed declarations recorded but not applied.
- **Severity:** WARNING Unresolved.
- **Unknown:** expressed constraints are not applied and `constraints` is INCOMPLETE. This reports known unsupported/unapplied commands, not general Tcl completeness.

### CST-010
- **Trigger:** precision fallbacks recorded by the connectivity/clock/reset model, grouped by category. A call to a user function falls back when its dependencies cannot be listed: a task or virtual method, a static local (unless the static local acts solely as an initialized induction variable of unrollable constant-bounded `for` loops: no declaration initializer, never written in loop bodies, never referenced outside an enclosing unrollable `for` loop that initializes it, within the 2048-walk scan budget), a call by reference, a system call that is not a pure function of its arguments, a function that does not set its return value on every path (a `case` counts only with every item and a `default`; constant-indexed element writes covering all bits of the return variable count across unconditional straight-line statements and unconditional constant-bounded `for` loops within the 2048-walk unroll budget). A function local read before it is assigned is not a fallback: it holds no signal, so no dependency is lost (LNT-044 reports the read).
- **Severity:** WARNING Unresolved.
- **Unknown:** results involving a fallback are inconclusive and `graph-precision` is INCOMPLETE. Constructs ignored without a recorded fallback are not enumerated.

### CST-011
- **Trigger:** (a) each modeled clock domain without a matching clock constraint; (b) when a constraint file is supplied, each top-level input captured by a register (as data, not as a clock or asynchronous reset) that no `set_input_delay` names; one per input, at its first capturing procedure.
- **Severity:** WARNING Unresolved.
- **Unknown:** (a) crossings involving that clock remain unknown; (b) the input is analysed as asynchronous to every clock. Either marks `constraints` INCOMPLETE.

### CST-012
- **Trigger:** (a) each top-level output port or bit matched by a valid `set_output_delay -clock X`; trace its cone to launching registers and report the output boundary; (b) when constraints are supplied, each uncontracted top-level output port or omitted bit carrying a synchronized or multi-domain CDC-relevant value (constant-driven uncontracted padding bits do not require an output contract); and (c) when distinct recognized synchronizer source bits appear on separate single-source output bits of one port word under the same capture clock, report unresolved independent-field/coherence intent. Exact copies of one source and constant padding do not count as separate fields; outputs on different ports are assessed independently. A reconvergent bit does not prevent checking the remaining fields. Non-CDC single-clock outputs without `set_output_delay` do not require an output contract.
- **Severity:** INFO Established when the ordinary boundary cone is complete and synchronous to X; ERROR when a known asynchronous launch is in the contracted output cone. An incomplete cone, unknown/exclusive relation, uncontracted CDC-relevant output/bit, or packed-word field-intent finding is WARNING Unresolved (with proof `NOT_ATTEMPTED` on coherence and uncontracted-output obligations); a known async case may retain ERROR with Unresolved confidence. Do not downgrade a mixed asynchronous cone because a destination-domain selector or control is also present. Exact per-bit data-arm qualification is not currently established by the model.
- **Unknown:** input feed-through, no launch register, mixed/unknown domains, opaque calls, black-box, unresolved/undriven leaves, and uncontracted CDC-relevant output bits mark `constraints` INCOMPLETE. An output-delay declaration supplies the external capture clock independently for each matched contract; it does not prove a consumer protocol. The current dependency graph can lose arm-to-bit association for computed selects and shared hubs, so it cannot show that a particular asynchronous data bit is selected by a destination-domain qualifier. Keep the legacy ERROR whenever asynchronous launch evidence is present; preserve its existing confidence and INCOMPLETE state for opaque or otherwise unproven cones. A future downgrade requires exact data-path and selector evidence for the affected matched output bit under its own capture contract, plus a WARNING/INCOMPLETE stability and consumer-contract obligation. Physical timing is not proved. The word-intent check only uses bit-accurate known synchronizer provenance; opaque or unresolved sources do not establish additional fields. If an imprecise bit cone may observe a synchronizer, retain WARNING Unresolved for the dependency. SDC clocks and delays do not prove width, atomicity, field independence, or consumer expectations. Single-domain non-synchronized outputs without an output delay remain uncontracted (external capture assumptions outside CDC-relevant signals stay outside this check); CDC-029 reports a primary-input feed-through only when a capture contract identifies its output clock.

### CST-013
- **Trigger:** each active `create_generated_clock` whose target and `-source` resolve to design objects. Compares the declared `-source`, `-divide_by`, and `-invert` against bounded RTL tracing of exact aliases, combinational inversion, and cascaded single-bit toggle-flop (`q <= ~q`) integer dividers.
- **Exempt:** declarations whose target or `-source` fails to resolve (`CST-002`).
- **Severity:** INFO Established when the RTL derivation matches the declared source, divider, and inversion (`CDC-030` still preserves the timing/phase assumption disclosure). ERROR Established when the target is provably driven by a different source root or a different integer divide/invert factor — a proven source mismatch takes precedence over unsupported transform options (`-multiply_by`, `-edges`, phase/duty transforms). WARNING Unresolved when the source root is not disproven and the declaration uses `-multiply_by`, `-edges`, phase/duty transforms, clock mux/bypass paths, or any derivation the bounded RTL check cannot establish.
- **Unknown:** any ERROR or WARNING marks `constraints` INCOMPLETE; a generated-clock declaration never establishes a synchronous clock relationship on its own.

### CST-014
- **Trigger:** an applied binary `set_case_analysis` declaration emits a warning with fixed values and run totals of pruned conditional branches and mux expressions.
- **Severity:** WARNING Unresolved.
- **Unknown:** alternative values and crossings are not analyzed; `constraints` and `case-analysis-modes` are INCOMPLETE. Unapplied declarations are reported under CST-009, with object/conflict problems also under CST-018.

### CST-016
- **Trigger:** each source and clause of a supplied `reset_contracts` config section ([RDC workflow](RDC_WORKFLOW.md#scope-and-gate)): a source is bound to an exact top-input bit (`port`) or an exact traced reset root (`root`) of an asynchronous reset event; `assert_together` groups and initial `state` need bound ports; a `release_order` needs both ends bound and a clock naming one effective SDC primary clock (not replaced on its top-level source port by a later clock declaration without `-add`) with one RTL clock root (serialized as `clock_root` in `reset_contract_validation`). One finding per clause or failing source.
- **Exempt:** no `reset_contracts` section; a bound source used by a reported clause.
- **Severity:** INFO Informational for an applied environment assumption (bound group, bound initial state, port-to-port order): it is stated, not checked. WARNING Established for a demonstrated conflict (declared polarity against the RTL, two ids on one node, overlapping groups, cyclic or duplicate orders). WARNING Unresolved for an unused, unbound, unsupported (root-to-port) or unresolved/replaced-clock entry, for a clause that depends on a rejected source, and for every clause when `set_case_analysis` is present (including any release-order contradiction). A root-to-root or port-to-root order is checked by its RTL witness: a chain of L >= `RDC-006.min_stages` (default 2) zero-delay reset flops, each reset only by the earlier root A through an exact single-driver zero-delay wire/port copy and clocked by the exact release clock root and edge (no delayed continuous or net copy), whose last stage drives the later root B through an exact single-driver zero-delay wire/port copy (no `wor`/`wand`/`tri`, multiple drivers, opaque/black-box drivers, or `always_comb`/latch copies), with no fan-out from any non-final stage to anything other than the next stage's D (no primary output, opaque instance, clock, logic or async pin), with A (when internal) also having a single zero-delay driver, with every receiver of B clocked by the exact release clock root and edge, reset only by B under a plain unconditional guard with a decided binary forced value, and with neither A, B nor their raw startup inputs used as clocks directly or through exact single-driver aliases. The structural bound is L when A is a zero-delay NBA output on that exact clock root and edge and L-1 otherwise, and it needs an initial asserted state for A's raw input. INFO Established when every condition holds and the bound meets `min_cycles`. WARNING Established when the same sound pattern is reversed, or when an NBA release on the same edge gives L below `min_cycles` (unless `set_case_analysis` is present). Any other shape or failed prerequisite is WARNING Unresolved with the reason.
- **Unknown:** every Unresolved finding marks scope `reset-contracts` INCOMPLETE; a decided conflict does not. Combinational bypasses, counters, req/ack dependencies, delayed copies, wired/multi-driver nets, opaque taps, shallow chains and multi-clock stages stay Unresolved. Clock uses of A, B or their raw startup inputs through combinational logic (such as an inverter) and a later `create_clock` placed only on a downstream hierarchical pin (rather than the top-level clock port) are not detected by the structural check. Only explicit declarations are covered; implicit, unmodelled or scenario-dependent reset state is not claimed, and a run with no declarations or with every declaration checked is COMPLETE for this rule. Per-entry results are in the report input `reset_contract_validation`.

### CST-018
- **Trigger:** constraint parse problems, unresolved referenced objects/clocks or non-clock selectors in `set_clock_groups`, unresolved referenced objects/clocks in declarations (`set_input_delay`, `set_output_delay`, `set_case_analysis`, or path-exception `[get_clocks ...]`), or unreadable/invalid library or SDC files. For CDC boundary timing, every input or output delay target must match a supported port and the delay must name a declared clock.
- **Severity:** WARNING Unresolved for parsed problems and unresolved references; ERROR for unreadable or invalid library files and unreadable SDC files.
- **Unknown:** the affected declaration/file is not fully applied and `constraints` is INCOMPLETE. `set_units -time` supports only explicit s, ms, us, ns, ps and fs values; unsupported or malformed units are parse problems. Other semantic option-value validation is limited.

### CST-019
- **Trigger:** catalogued rules did not give their answer on this design (failed, not run, or degraded by it), or a programmatic/API `Config::disabledRules` entry names an unknown rule. On the CLI, `--disable-rule` validates rule IDs and globs up front and exits immediately with usage code `64` (writing no report) when a pattern matches no catalog rule. A rule that reports each site it cannot decide as its own Unresolved finding (LNT-057) has answered: the gap is in the report, counted against the not-analysed budget, so the rule is not also degraded. A rule that is partial or unimplemented in the tool version, or disabled by a valid ID/glob, is a run-level statement (`tool coverage`, `coverage.rules`), not a finding.
- **Severity:** WARNING Unresolved (or CLI exit code `64` up front for unknown `--disable-rule` patterns).
- **Unknown:** absence of findings for rules that did not answer is not evidence of safety; `analysis_coverage` is INCOMPLETE. Profile policy drift is not checked.

### CST-022
- **Trigger:** waiver expiry is invalid or past, scope has no narrowing criterion, or a waiver matches no finding in this run (including hierarchy scope that cannot safely split incomplete occurrence lists).
- **Severity:** a waiver that matches nothing (stale, or only findings whose instance list is incomplete) is INFO, Established: it waives nothing and changes no hardware, and a finding it meant to cover is open at its own severity. An invalid, expired or overbroad waiver is WARNING Unresolved and is not applied, because its author believes it is in force.
- **Unknown:** stale, overbroad, or expired waivers do not suppress findings; validation does not mark an analysis scope INCOMPLETE.


### Accuracy-case adaptations

Reduced and modified RTL under `tests/accuracy/cases/CST/` exercises clock dividers and muxes, synchronous interfaces, FIFO boundaries and clock monitoring. The upstream licences apply to these adaptations; each case retains its attribution and records the exact source path and changes in `label.json`. Whole upstream designs are not vendored. Licence texts and notices ship in [tests/accuracy/LICENSE](../../tests/accuracy/LICENSE).

| Source at pinned commit | Licence | Material taken |
|---|---|---|
| [pulp-platform/common_cells](https://github.com/pulp-platform/common_cells/tree/e73baaec2ca665cd80c3c384e9258e35242b829c) | SHL-0.51 | Reduced RTL fragments used as CST test inputs and mutated lookalikes |
| [lowRISC/ibex](https://github.com/lowRISC/ibex/tree/2373e5dadcfc4f56672962fa242f6a051acc9aca) | Apache-2.0 | Reduced RTL fragments used as CST test inputs and mutated lookalikes |
| [lowRISC/opentitan](https://github.com/lowRISC/opentitan/tree/92f3d03b0dcf6d5c1933a65d1497747f67d19f4c) | Apache-2.0 | Reduced RTL fragments used as CST test inputs and mutated lookalikes |
| [alexforencich/verilog-axis](https://github.com/alexforencich/verilog-axis/tree/48ff7a7e2ef782cf778d47910cf85835c64b1bce) | MIT | Reduced RTL fragments used as CST test inputs and mutated lookalikes |
