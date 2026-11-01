# LNT - Semantic, structural and synthesizability lint

Titles must match `catalog.json` (a unit test checks this). The specification bullets below are the contract each rule is tested against
(`tests/fixtures/<RULE-ID>/`: good, bad, boundary, hierarchy/unknown cases). Rules marked *slang* map slang's own diagnostics (by option name
or error code, see `src/frontend/slang_rule_map.cpp`); nothing slang reports is dropped. A rule with a `gap` in `catalog.json` is Partial; gaps are not repeated here.

| ID | Required check |
|---|---|
| `LNT-001` | Syntax, preprocess and elaboration failure |
| `LNT-002` | Missing/duplicate module, package and symbol references |
| `LNT-003` | Illegal port connections (unknown, duplicate, mixed or mismatched); unconnected ports are LNT-010 |
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
| `LNT-014` | always_ff / always_comb procedure misuse; driver conflicts are LNT-009 |
| `LNT-015` | Blocking assignment to state in a clocked block |
| `LNT-016` | casex statement |
| `LNT-017` | Incomplete FSM case coverage (enum-typed state: every enumerator handled) |
| `LNT-018` | Constant condition or unreachable branch |
| `LNT-019` | Non-synthesizable construct in design RTL |
| `LNT-020` | Register assigned only in its reset branch (reset-only) |
| `LNT-022` | One signal controls a register on both posedge and negedge |
| `LNT-024` | Suspicious undriven output, partially assigned struct/array |
| `LNT-025` | Later conditional write overrides an earlier one when both conditions can hold |
| `LNT-026` | Sized literal whose digits need more bits than its size (outside a concatenation) |
| `LNT-027` | Clock net used as data (reaches a register data input or an output) |
| `LNT-029` | Enum FSM state never assigned, or case arm without a transition |
| `LNT-031` | Illegal or unsafe assignment to net / variable kind |
| `LNT-032` | Duplicate or overlapping case arms |
| `LNT-033` | Suspicious `unique`, `unique0`, `priority` claims |
| `LNT-034` | Conditional expression width/sign ambiguity |
| `LNT-035` | Shifted or unsized operand widened by its context, and out-of-range shift counts |
| `LNT-036` | Product narrower than its operands, or carry lost by a self-determined addition |
| `LNT-037` | Divide or modulo by zero where statically provable |
| `LNT-038` | Comparison with itself or with a constant outside the operand range, and contradictory or tautological boolean pairs (a && !a, a || !a) |
| `LNT-039` | Unintentional X/Z assignments in synthesizable RTL |
| `LNT-040` | X/Z literals in comparisons, case items, inside sets and conditions; ===/!== |
| `LNT-041` | Incomplete array, struct, union or interface assignments |
| `LNT-042` | Partial or overlapping output drive through hierarchy |
| `LNT-044` | Sequential variable read before intended assignment |
| `LNT-045` | Combinational sensitivity mismatch in legacy `always` |
| `LNT-047` | Data-driven clock, asynchronous control or event expressions (inferred gated clocks) |
| `LNT-048` | ASIC versus FPGA initialization assumptions |
| `LNT-049` | Suspicious inferred RAM or mixed memory access semantics |
| `LNT-050` | Input/output port direction or interface modport misuse |
| `LNT-051` | Unreachable generate alternatives for selected configuration |
| `LNT-053` | Write overwritten before any read |
| `LNT-054` | Register with neither reset nor initial value read in a condition |
| `LNT-055` | Unsigned difference compared below zero |
| `LNT-056` | Constant concatenation or replication whose width differs from its assignment target |
| `LNT-057` | Parameter-dependent truncation at an assignment or port connection |
| `LNT-058` | Asynchronous control polarity does not match its edge |
| `LNT-059` | Unused package imports / declarations and shadowing |
| `LNT-060` | Blocking write in a clocked process read by another process |
| `LNT-061` | Dependency on non-synthesizable system tasks/functions |
| `LNT-062` | Nonblocking assignment in a combinational block |
| `LNT-063` | One variable assigned with both blocking and nonblocking assignments |
| `LNT-064` | casez or casex item whose bits are all wildcards |
| `LNT-065` | Case statement without default or with uncovered values |
| `LNT-066` | X or Z constant that reaches the condition of an assignment (flow-insensitive) |
| `LNT-067` | Register without a reset value (assigned only outside the reset branch) |
| `LNT-068` | always_ff block without reset in a module whose other always_ff blocks reset |
| `LNT-069` | Edge control not tested by the first if / else-if chain (clock-like or untested asynchronous control) |
| `LNT-070` | Reset branch of an asynchronous reset assigns a non-constant value |
| `LNT-071` | FSM reachability and liveness obligation (proof not attempted) |
| `LNT-072` | Reset net feeding the logic that generates a clock |
| `LNT-073` | Constant assigned to a narrower target changes its value |
| `LNT-074` | Unsized literal compared with an operand wider than 32 bits |

## Specification

Slang-backed rules (named below as slang options or codes) are slang's own diagnostics, converted in `src/frontend/frontend.cpp`: severity copied (Note INFO, Warning WARNING, Error ERROR, Fatal FATAL), confidence Established, `hierarchy` taken from the slang symbol, evidence kinds `macro_expansion`, `slang_note` and `occurrence_list_incomplete`, no `signals`. An option name is mapped in `src/frontend/slang_rule_map.cpp`; an error without option name by its diagnostic code (`ruleForSlangDiag`).

### LNT-001
- **Trigger:** slang diagnostics without an option name whose code is not mapped to another LNT rule (syntax, preprocess and other elaboration errors); load failures and internal failures while loading; the summary finding when the design has at least one slang error.
- **Exempt:** slang's too-many-errors fatal (not a design error); mapped diagnostics (reported under their own rule).
- **Severity:** FATAL for load failures, internal failures and the elaboration-error summary; slang's severity (ERROR, FATAL) for each unmapped diagnostic, Established.
- **Unknown:** none: decided by slang. A design with elaboration errors sets upstream-backed rules to Partial (`degradedReason`).

### LNT-002
- **Trigger:** slang diagnostic codes UnknownPackage, UnknownModule, UnknownInterface, UnknownPrimitive, UndeclaredIdentifier, TypoIdentifier, UnknownClassOrPackage, UsedBeforeDeclared, UnknownMember, UnknownPackageMember, UnknownClassMember, HierarchicalRefUnknownModule, UndeclaredButFoundPackage, AmbiguousWildcardImport, ImportNameCollision, NotAType, NotAValue, NotAHierarchicalScope, MultipleTopDupName, CouldNotResolveHierarchicalPath, PackageExportNotImported, DefinitionUsedAsType, DefinitionUsedAsValue; options redefinition, redefinition-different-type, duplicate-definition, unknown-library.
- **Severity:** slang's severity (ERROR for unresolved references), Established.
- **Unknown:** none: decided by slang.

### LNT-003
- **Trigger:** slang diagnostic codes MixingOrderedAndNamedPorts, DuplicateWildcardPortConnection, DuplicatePortConnection, TooManyPortConnections, PortDoesNotExist, PortConnDimensionsMismatch, InterfacePortNotConnected, InterfacePortTypeMismatch, ImplicitNamedPortNotFound, PortConnArrayMismatch, InOutVarPortConn, InOutUWireConn, ModportConnMismatch, RefPortUnconnected, RefPortUnnamedUnconnected, ImplicitConnNetInconsistent, MismatchedUserDefPortConn, MismatchedUserDefPortDir, InterfacePortInvalidExpression, InvalidHierarchicalIfacePortConn; options implicit-port-type-mismatch, port-coercion, net-inconsistent.
- **Exempt:** unconnected ports (LNT-010).
- **Severity:** slang's severity, Established.
- **Unknown:** none: decided by slang.

### LNT-004
- **Trigger:** slang options width-trunc, width-expand, port-width-trunc, port-width-expand.
- **Exempt:** explicit casts (slang does not warn on them).
- **Severity:** slang's severity (WARNING), Established.
- **Unknown:** none: decided by slang.

### LNT-005
- **Trigger:** slang options implicit-conv, packed-array-conv, sign-conversion, sign-compare, ineffective-sign.
- **Exempt:** explicit casts.
- **Severity:** slang's severity (WARNING), Established.
- **Unknown:** none: decided by slang.

### LNT-006
- **Trigger:** slang options index-oob, range-oob, range-width-oob; slang codes IndexValueInvalid, RangeWidthOverflow, ScopeIndexOutOfRange. Constant selects only.
- **Exempt:** dynamic indexes (not decidable by slang; no finding).
- **Severity:** slang's severity (WARNING or ERROR), Established.
- **Unknown:** none: decided by slang.

### LNT-007
- **Trigger:** slang options implicit-net, implicit-net-port.
- **Severity:** slang's severity (WARNING), Established.
- **Unknown:** none: decided by slang.

### LNT-008
- **Trigger:** a user-declared module-level net or variable (not a port) that is integral and not a struct, union or array, with at least one driver (or a write through a non-constant index), whose static driver bit ranges leave some bit of its selectable width undriven.
- **Exempt:** ports (LNT-024); aggregates (LNT-041); symbols with no driver at all (slang reports these as undriven-net, undriven-port or unassigned-variable, all mapped to LNT-008).
- **Severity:** WARNING, Established, for a static gap.
- **Unknown:** WARNING, Unresolved ("cannot determine which bits ... are driven") when a write uses a module-level index, a driver is not fully static, or a loop-indexed write exists and a gap is found.

### LNT-009
- **Trigger:** (a) slang options multiple-cont-assigns, multiple-always-assigns, mixed-var-assigns and slang codes MultipleUWireDrivers, MultipleUDNTDrivers; (b) a module-level net of kind wire, tri, tri0, tri1 or trireg with two continuous assignment drivers (not instance outputs) that drive an overlapping bit range, reported at the later assignment once per net.
- **Exempt:** a driver whose right-hand side is a constant that is all z; a tri-state pair whose enables are provably exclusive (`en ? v : 'z` against `!en ? w : 'z`); wand, wor, triand and trior nets; instance-output overlaps (LNT-042). A driver with x bits or with only some z bits drives and is not exempt.
- **Severity:** (a) slang's severity; (b) WARNING, Established, when no driver has an all-z literal in its assignment; WARNING, Unresolved, when a driver has one and the enables are not proven exclusive.
- **Unknown:** (b) with an all-z literal in a driver: "driver conflict cannot be excluded" (Unresolved, above).

### LNT-010
- **Trigger:** (a) slang options unconnected-input-port, unconnected-output-port, unconnected-inout-port, unconnected-unnamed-port, empty-input-connection, empty-output-connection, empty-inout-connection; (b) an empty entry in a positional instance connection list in user code (`inst(a, , c)`), one finding per empty entry.
- **Exempt:** named connections (covered by slang); deliberate empty connections are reported too (waive with a reason).
- **Severity:** (a) slang's severity (WARNING); (b) WARNING, Established.
- **Unknown:** none: decided by slang for (a); (b) is decided by the syntax tree.

### LNT-011
- **Trigger:** (a) slang options unused-def, unused-net, unused-implicit-net, unused-variable, unused-but-set-net, unused-but-set-variable, unused-but-set-port, unused-port, unused-argument, unused-parameter, unused-type-parameter, unused-typedef, unused-genvar; (b) a variable, net or parameter in user code carrying the attribute `(* unused *)` or `(* maybe_unused *)`, reported as an INFO marker.
- **Exempt:** for (a): none beyond slang's own rules. For (b): slang does not report marked objects, so the marker is reported here instead.
- **Severity:** (a) slang's severity (WARNING); (b) INFO, Informational.
- **Unknown:** none: decided by slang for (a).

### LNT-012
- **Trigger:** on the shared model (combinational edges only; register and async-control edges end a path): (a) a bit that depends on itself through combinational logic (self-dependency, recorded per procedure or assignment); (b) a strongly connected component of more than one node, through ports and hierarchy.
- **Exempt:** paths through registers (Seq and AsyncCtl edges).
- **Severity:** (a) WARNING, Heuristic, in a combinational or latch procedure; otherwise ERROR, Established. (b) ERROR, Established, when the cycle spans processes or assignments; WARNING, Heuristic, when all its edges are in one combinational or latch procedure or the cycle contains a collapsed (wide) signal.
- **Unknown:** signals wider than the per-bit limit are one node: a cycle through them is WARNING, Heuristic ("wide signals are tracked as one node").

### LNT-013
- **Trigger:** (a) slang option inferred-latch (always_comb and slang-analysed latches); (b) a legacy `always` block in user code with style combinational (`always @*`) or level (`always @(a or b)`) in which a module-level variable written by the block has bit ranges that can still hold the value from before the block on some path at its end (`Flow::unassignedAtEnd`). When no such range exists, the block is still reported if its path analysis is opaque (a user subroutine call or a mid-body timing control) or approximated (a `continue`, or a plain case with a constant item that has x or z bits or is wider than the selector).
- **Exempt:** for (b): block-local variables, `always_comb`, `always_latch` and edge-triggered blocks.
- **Severity:** (a) slang's severity; (b) WARNING, Heuristic, for a range that can hold the entry value, Unresolved when the block is opaque; with no such range, WARNING, Unresolved, when the block is opaque or approximated.
- **Unknown:** (a): none, decided by slang. (b) an opaque or approximated block with no such range: WARNING, Unresolved ("assignment on every path cannot be proven"). A procedure slang cannot analyse (`Flow::bad`): WARNING, Unresolved, once per procedure.

### LNT-014
- **Trigger:** slang codes BlockingInAlwaysFF, AlwaysFFEventControl, ForkJoinAlwaysComb, AlwaysWithoutTimingControl.
- **Exempt:** none. Driver conflicts are LNT-009.
- **Severity:** ERROR, Established (slang's severity).
- **Unknown:** none: decided by slang.

### LNT-015
- **Trigger:** a blocking assignment whose target is a module-level variable inside an edge-triggered procedure (`always_ff`, or `always` with an edge event). A select of a variable resolves to its base symbol (an unpacked-array element or packed select resolves to its base symbol), and every operand of a concatenation target is a target; one finding per target variable.
- **Exempt:** block-local temporaries; combinational, latch (`always_latch`), initial and final procedures; nonblocking assignments; struct member targets (the target symbol is the member, which is not module-level).
- **Severity:** WARNING, Established.
- **Unknown:** none (decided by the AST).

### LNT-016
- **Trigger:** a `casex` statement in user code (X and Z in the case expression are wildcards).
- **Exempt:** `casez` and `case`.
- **Severity:** WARNING, Established.
- **Unknown:** none (decided by the AST). Slang's case-default, case-incomplete, case-none and case-not-wildcard diagnostics are reported as LNT-065, not here.

### LNT-017
- **Trigger:** slang options case-enum and case-enum-explicit: an enumerator of an enum-typed case expression has no explicit item.
- **Exempt:** non-enum selectors (case-incomplete is LNT-065).
- **Severity:** slang's severity, Established.
- **Unknown:** none: decided by slang.

### LNT-018
- **Trigger:** in user code, (a) an `if` with one condition and no pattern whose condition is a constant (evaluates without a runtime reference) and whose taken branch leaves the other branch unreachable (constant false with a then branch, or constant true with an else); (b) a `?:` with a constant condition (the unselected operand is unreachable); (c) a `case` whose expression is constant and whose known branch (`getKnownBranch`) leaves an item or the default unreachable; (d) slang diagnostic StaticAssert (static assertion failed).
- **Exempt:** a constant-true `if` with no else (nothing unreachable); generate-if (LNT-051); loop conditions; conditions that depend on a variable.
- **Severity:** WARNING, Established, for a literal-only constant; INFO, Established, when the condition or case expression references a parameter or genvar, and INFO, Heuristic, for a lone 0/1 literal in an `if` (the disable idiom); (d) slang's severity, Established.
- **Unknown:** WARNING, Unresolved, for a constant `case` expression with non-constant items (which arms are reachable is not decided) and for a constant condition with a pattern or several conditions ("not analysed").

### LNT-019
- **Trigger:** in user code: `#` delays (statement, intra-assignment, continuous assignment); `wait`, `wait fork`, `wait_order`; `fork`/`join` blocks; `disable fork`; event trigger `->`; `force`/`release`; procedural `assign`/`deassign`; `forever`; `for` without a condition; `for`, `repeat`, `while` and `do-while` with a bound that is not constant (a relational test of a name against a constant is accepted for `for`); `program` blocks; class, covergroup, and real, shortreal, string, chandle, dynamic-array, queue and associative variables (class properties are reported with their class).
- **Exempt:** constructs inside assertions; `initial` blocks (LNT-048); class properties.
- **Severity:** WARNING, Established; INFO, Established, inside final blocks and in top-level instances without ports (testbench-like).
- **Unknown:** a non-constant bound containing a user function call: WARNING, Unresolved; any other non-constant bound: WARNING, Established.

### LNT-020
- **Trigger:** a sequential block whose asynchronous reset is identified from its edge list (two edge controls, the reset tested by the first if / else-if chain, or chosen by name when neither control is tested) and whose reset branch assigns a module-level variable that the other branch does not assign.
- **Exempt:** blocks without an asynchronous control (single-edge blocks, including synchronous-reset blocks, are not analysed); blocks with more than two edge controls; variables assigned in both branches.
- **Severity:** WARNING, Established when the reset branch comes from a direct test of the reset with matching polarity; WARNING, Heuristic otherwise (the true branch is taken as the reset branch).
- **Unknown:** name-chosen resets and compound tests: Heuristic (above).

### LNT-022
- **Trigger:** one signal appears as both a posedge and a negedge control of one event list (`always` or `always_ff` with at least two edge controls).
- **Exempt:** controls on different signals (untested controls are LNT-069).
- **Severity:** WARNING, Established.
- **Unknown:** none.

### LNT-024
- **Trigger:** an output port of a user module whose internal symbol has no driver (and no write through a non-constant index), or whose static driver bit ranges (or struct members / array elements) leave a gap.
- **Exempt:** input and inout ports; aggregate ports are covered here, not by LNT-041 (which skips ports).
- **Severity:** WARNING, Established, for "never driven" and for undriven bits, members or elements.
- **Unknown:** WARNING, Unresolved, for a dynamic, queue or associative output ("coverage not modelled"), a module-indexed write, a non-static driver, or a loop-indexed write with a gap.

### LNT-025
- **Trigger:** within one procedure, an earlier conditional write v and a later conditional write w of the same variable, of the same write kind (blocking in combinational, latch and level procedures; nonblocking in clocked procedures), with both writes static (exact bit ranges), v's bit range inside w's range, both guard lists non-empty, neither write inside a loop body, no read of the variable that can see v (`!readBefore`), no timing control between them (`!observed`), neither guard list implying the other, and `!exclusive(v, w)`: when both conditions hold, w replaces v's bits.
- **Exempt:** conditions exclusive by structure or by comparing one operand with different constants (`Flow::exclusive`; the comparison proof is withdrawn when that operand is written earlier in the procedure); nested or dominating writes (LNT-053 or intended priority); unconditional writes (LNT-053); compound writes.
- **Severity:** WARNING, Heuristic (the conditions may still be exclusive); Unresolved when a user subroutine call lies between the two writes (`exposed`).
- **Unknown:** none emitted for the conditions: a call inside a condition is not checked and the finding is kept. A procedure slang cannot analyse (`Flow::bad`): WARNING, Unresolved, once per procedure.

### LNT-026
- **Trigger:** in user code, a sized integer literal outside any concatenation or replication whose digits, as written in the source text, need more bits than its size (a signed decimal literal spends one bit on the sign).
- **Exempt:** unsized literals; fill literals ('0, '1, 'x, 'z); literals inside a concatenation or replication (LNT-056).
- **Severity:** WARNING, Established.
- **Unknown:** digits not in one source buffer (for example from a macro), decimal digits containing x, z or ?, or more than 128 bits: WARNING, Unresolved ("could not be checked").

### LNT-027
- **Trigger:** on the shared model, a clock domain root that reaches a register data input (Seq edge) or a top-level output through combinational edges.
- **Exempt:** paths that end at clock pins only (clock-gating logic); clock-to-clock paths; async-control edges.
- **Severity:** WARNING, Established.
- **Unknown:** paths end at black-box roots and are not followed into the cell.

### LNT-029
- **Trigger:** a registered FSM: every case statement in user code whose selector is of an enum type that a clocked procedure assigns (initialiser values do not count). For each such case statement: (a) an enumerator that no constant assignment or initialiser of the type produces (dead-state candidate), reported on the case; (b) a case item with a single expression and a non-empty statement that assigns no variable of the type, in a case with no assignment to a variable of the type textually before it in its procedure (stuck-state candidate), reported on the item.
- **Exempt:** (a) is suppressed for a type that has any assignment from a non-constant source; (b) is suppressed by an assignment to a variable of the type that precedes the case in its procedure (a default assignment; a default item does not suppress it); enum types not assigned in clocked code.
- **Severity:** WARNING, Heuristic.
- **Unknown:** a non-constant source for the type removes the dead-state claim (no finding for (a)); reachability and liveness are LNT-071.

### LNT-031
- **Trigger:** slang codes AssignToNet, BadProceduralAssign, BadForceNetType, ExpressionNotAssignable, AssignmentToConstVar, CantModifyConst, SelectOfVectoredNet; slang option bad-procedural-force.
- **Severity:** ERROR, Established (slang's severity).
- **Unknown:** none: decided by slang.

### LNT-032
- **Trigger:** a user-code `case inside` statement in which some item (a constant value or a constant range lo:hi) matches a value that an earlier item also matches (range intersection, or value equal on the bits of both). Reported once per later overlapping item.
- **Exempt:** non-overlapping items; `case`, `casez` and `casex` duplicate, overlapping and unreachable items (slang case-dup, case-overlap, case-unreachable).
- **Severity:** WARNING, Established.
- **Unknown:** WARNING, Unresolved ("'case inside' overlap not determinable"), reported once for the statement, when an item is not an integer constant.

### LNT-033
- **Trigger:** a `unique`, `unique0` or `priority` case or if statement in user code. Case: (a) two items of a unique or unique0 case match a common value (overlap); (b) a unique or priority case without default leaves a value uncovered, where the values are the enumerators of an enum selector or every value of a selector of at most 16 bits (X/Z added for a four-state selector). If: (c) a unique or priority chain has no final else; (d) a unique or unique0 chain has two or more conditions that are constant true.
- **Exempt:** unique0 case: no coverage check; priority case: no overlap check; a case with a default item: no coverage check; priority if: no overlap check; unique0 if chain without else: no finding.
- **Severity:** ERROR, Established, for (a) and (d); WARNING, Established, for (b) and (c); INFO, Unresolved, for the not-checkable cases below.
- **Unknown:** INFO, Unresolved ("claim not statically checkable") when a case item or an if condition is not a constant, when the selector is wider than 64 bits, or when a non-enum selector wider than 16 bits has no default (coverage not enumerated).

### LNT-034
- **Trigger:** in user code, a `?:` whose arms have integral types and either (a) differ in width, when neither arm is an unsized literal; or (b) differ in signedness, when a signed arm is a non-literal or a negative literal.
- **Exempt:** fill literals; unsized based literals (fixed 32-bit unsigned); a non-negative decimal literal against an unsigned arm; arms without integral width.
- **Severity:** WARNING, Established, one finding per conditional for (a) and (b); INFO, Established, for an unsized signed decimal literal arm against a non-literal arm wider than 32 bits.
- **Unknown:** none.

### LNT-035
- **Trigger:** in user code, (a) an unsized literal as the left operand of `<<` or `<<<`, in a concatenation or replication operand position, whose 32-bit result is widened by the container or the enclosing assignment target beyond 32 bits: WARNING, Heuristic; (b) a non-literal, non-constant signed left operand of `<<` or `<<<` with a non-constant shift amount, whose expression is wider than the operand: INFO, Heuristic. Plus slang options shift-count-overflow, shift-count-negative, signed-logical-shift and unsigned-arith-shift.
- **Exempt:** context-determined shifts such as `wide = 1 << n` (not reported); constant operands and constant amounts; `>>` on signed values (slang); fill literals.
- **Severity:** (a) WARNING, Heuristic; (b) INFO, Heuristic; slang options: slang's severity.
- **Unknown:** none.

### LNT-036
- **Trigger:** in user code, (a) a `*` whose evaluated width is less than the sum of its operand widths (a constant operand counts its significant bits): WARNING, Heuristic ("product truncated"); (b) a `+` or `-` that is an operand of a concatenation (not a replication) with two non-constant operands whose result width equals the wider operand's width: WARNING, Heuristic; (c) only with option LNT-036.report_modular=true: a non-compound `+` or `-` assigned to a target of exactly the operand width, both operands non-constant: INFO, Heuristic.
- **Exempt:** constant operands; non-integral (real) arithmetic; compound assignments.
- **Severity:** WARNING, Heuristic, for (a) and (b); INFO, Heuristic, for (c).
- **Unknown:** (b) also fires on zero-extended operands such as `{1'b0, a} + {1'b0, b}`, where no carry is lost (Heuristic).

### LNT-037
- **Trigger:** slang option divide-by-zero: a constant divisor of zero.
- **Exempt:** variable divisors (slang evaluates constants only).
- **Severity:** slang's severity, Established.
- **Unknown:** none: decided by slang.

### LNT-038
- **Trigger:** in user code, (a) a comparison (`==`, `!=`, `===`, `!==`, `<`, `<=`, `>`, `>=`) of a plain runtime reference (identifier or select with a pure index) with an integer constant of at most 62 bits and no X/Z bits, when the result is the same for every value the reference can take in its signed or unsigned domain; (b) a comparison of one plain runtime reference with itself, true for `==`, `===`, `<=`, `>=` and false otherwise; (c) `a && !a` (false) or `a || !a` (true) on one plain runtime reference.
- **Exempt:** comparisons where both operands are constant (LNT-018 domain); operands that are calls, arithmetic or selects with a non-pure index (not analysed); X/Z and real constants (silent here; LNT-040 covers X/Z comparisons).
- **Severity:** WARNING, Established; INFO when the constant operand references a parameter or genvar.
- **Unknown:** WARNING, Unresolved ("comparison is outside the modelled domain") when the constant is wider than 62 bits, the operand is wider than 62 bits, or a negative constant is compared with an unsigned operand.

### LNT-039
- **Trigger:** in user code, an X or Z literal (unbased unsized, or sized with x or z bits) that is the value of an assignment right-hand side, a continuous assignment, a variable or net initialiser, a return value, or a `?:` arm, or a parameter with X or Z bits used in such a position.
- **Exempt:** literals in comparisons (LNT-040); none otherwise.
- **Severity:** WARNING, Established; INFO, Established, for an X or Z in a case default item (don't-care intent) and for 'z assigned to a net (tri-state); INFO for any of these inside a final block or assertion.
- **Unknown:** none (literal-only detection).

### LNT-040
- **Trigger:** in user code, (a) `===` or `!==` (any operands); (b) `==` or `!=` with an X or Z literal in either operand; (c) an `inside` expression with an X or Z literal in its left operand or in a set element; (d) a plain `case` item (not casez or casex) with an X or Z literal; (e) a 1-bit X or Z literal as an `if` or `?:` condition. Plus slang option casez-with-x.
- **Exempt:** casez and casex items (wildcards); `==?` and `!=?`; `case inside` items.
- **Severity:** WARNING, Established; INFO inside final blocks and assertions.
- **Unknown:** none (literal-only detection). Slang's casez-with-x: slang's severity.

### LNT-041
- **Trigger:** a user-declared struct, union, fixed unpacked array or multi-dimension packed array variable or net, not a port, whose undriven bit set (complement of its static driver ranges) is non-empty so that some member or element has no driven bit; or an interface-instance signal read in the design with no driver at all.
- **Exempt:** ports (LNT-024); plain vectors and scalars (LNT-008); dynamic, queue and associative symbols emit nothing here.
- **Severity:** WARNING, Established, for a member or element never driven or partly undriven, and for an interface member read and never driven.
- **Unknown:** WARNING, Unresolved, for a module-indexed write, a non-static driver, or a loop-indexed write with a gap; each undriven member or element is named only when the coverage is decided.

### LNT-042
- **Trigger:** a user-declared net or variable with an output-port driver (instance output connection) where either (a) another continuous driver, not an output port, drives some of the same bits, reported at the later source position; or (b) the output-port driver covers a strict slice and some bits of the symbol have no driver at all (any driver).
- **Exempt:** for (a) only: tri, triand, trior, tri0, tri1, trireg, wand and wor nets, and continuous drivers whose RHS contains a 'z literal; procedural drivers are not compared.
- **Severity:** WARNING, Established, for (a); INFO, Established, for (b).
- **Unknown:** WARNING, Unresolved, "overlap and partial drive are not determinable", when a non-constant index writes the symbol or one of its drivers is not fully static.

### LNT-044
- **Trigger:** a module-level variable read in a combinational or clocked procedure where the read sees the value from before the procedure on every path (`entryOnly`), and a later whole, non-compound write of the same variable exists in the procedure. Combinational: any later whole write. Clocked: the later write must be blocking and belong to a different assignment than the read.
- **Exempt:** block-local variables; reads that see an assigned value on some path; clocked: a later nonblocking write; clocked: the read and write in one assignment (`cnt = cnt + 1` counter idiom).
- **Severity:** WARNING, Established. One finding per variable per procedure, at the first such read.
- **Unknown:** a procedure slang cannot analyse (`Flow::bad`): WARNING, Unresolved, once per procedure. Otherwise none emitted. A write made inside a called subroutine is not an access of the procedure, so such a read is still reported.

### LNT-045
- **Trigger:** a legacy `always @(...)` in user code with an explicit list of entries and no edges. Each entry is a signal, a select or a member of one: a static select lists its bit range, a dynamic select lists the whole signal. (a) A module-level signal with a bit that the body may read before assigning it (the value from before the activation on some path), where that read is not covered by the listed ranges: WARNING. (b) A listed signal that the body never reads: INFO.
- **Exempt:** `always @*`, `always_comb`, `always_latch` and edge-sensitive blocks; block-local variables; for (b), reads of an element or member count as reads of the base signal.
- **Severity:** WARNING, Established, for (a); INFO, Established, for (b).
- **Unknown:** a user subroutine call in the body (opaque): WARNING, Unresolved, and (b) is then not reported; signals read inside the subroutine are not checked. An entry with no root signal (an expression or a call): WARNING, Unresolved; the other entries are still checked.

### LNT-047
- **Trigger:** an edge event of a sequential block in user code that is (a) not a plain signal reference (an operator or call inside the event expression): WARNING, Established; or (b) a plain signal whose drivers include a continuous assignment, an `always_comb`, a level `always` or an `always_latch` whose assignment to it is not a plain copy: WARNING, Heuristic.
- **Exempt:** input ports; drivers that are registers (`always_ff` or edge-triggered blocks); plain alias drivers (continuous assignment or procedural assignments whose right-hand side is a plain signal or select).
- **Severity:** WARNING; Established for (a), Heuristic for (b).
- **Unknown:** no analysis object available: WARNING, Unresolved, for a plain edge signal.

### LNT-048
- **Trigger:** (a) a module-level static variable (not a net, not an unpacked array) with a declaration initialiser; (b) each assignment in an `initial` block whose target is a module-level symbol.
- **Exempt:** nets; unpacked arrays with an initialiser; automatic variables.
- **Severity:** WARNING for profiles asic and generic; INFO for profile fpga; Established, or Unresolved for profile generic (depends on the target).
- **Unknown:** profile generic: WARNING, Unresolved (above).

### LNT-049
- **Trigger:** a module-level unpacked-array variable (memory): (a) written by more than one process (more than one non-input-port driver container): WARNING, Heuristic; (b) written by a continuous assignment, `always_comb` or `always_latch`: WARNING, Established; (c) read by a continuous assignment or by an unclocked procedure (not initial or final): INFO, Informational; (d) read and written in one clocked process: INFO, Informational, proof NOT_ATTEMPTED.
- **Exempt:** a memory with a single clocked writer whose reads are all clocked.
- **Severity:** WARNING, Heuristic, for (a); WARNING, Established, for (b); INFO, Informational, for (c) and (d).
- **Unknown:** (d) read-during-write behaviour depends on the target RAM and is an obligation (NOT_ATTEMPTED), not modelled.

### LNT-050
- **Trigger:** slang codes InputPortAssign, InOutPortCannotBeVariable, InOutUWirePort, DirectionOnInterfacePort, DirectionWithInterfacePort, InvalidModportAccess, NotAllowedInModport, RefPortMustBeVariable, WriteToInputClockVar.
- **Severity:** ERROR, Established (slang's severity).
- **Unknown:** none: decided by slang.

### LNT-051
- **Trigger:** a generate if, else-if, else or case construct (not a loop generate) in user code: (a) each branch that is not elaborated in the selected configuration; or (b) a construct none of whose branches is elaborated.
- **Exempt:** loop generate blocks; a construct with at least one elaborated branch gives no WARNING (its other branches still give (a)).
- **Severity:** INFO, Informational, per uninstantiated branch (message gives the evaluated condition for if branches); WARNING, Established, for (b).
- **Unknown:** none: elaboration is exact for the selected configuration.

### LNT-053
- **Trigger:** a Kill (flow::Flow) whose victim and killer are both exact writes (a static select or the whole variable), neither compound, of the same kind (blocking or nonblocking), with the victim's bit range inside the killer's range, neither inside a loop body (walked once or unrolled), with no read of the victim before the killer (`!readBefore`), no timing control between them (`!observed`), and the killer's guards implied by the victim's (the overwrite runs whenever the first write ran).
- **Exempt:** the default-then-override idiom (`z = 0; if (c) z = a;`, the overwrite is conditional); a victim read in between (a nonblocking write does not change what later reads see, so a read between two nonblocking updates does not keep the first live); a victim exposed to another process by a timing control; final procedures. A dynamic select, or a victim only partly inside the killer's range, is not reported.
- **Severity:** WARNING. Established when victim and killer have equal guards; Heuristic ("on at least one path") when the killer encloses the victim; Unresolved when a user subroutine call between them may have read the value (`exposed`).
- **Unknown:** values behind user subroutine calls (Unresolved, above). A procedure slang cannot analyse (`Flow::bad`): WARNING, Unresolved, once per procedure.

### LNT-054
- **Trigger:** a module-level register assigned in clocked code with no reset (no asynchronous control, and no first-if branch that assigns only constants) and no declaration initialiser, which is read in an `if`, `case` or `?:` condition in user code; one finding per register, at its first such condition.
- **Exempt:** registers assigned with a reset in any clocked block; registers with an initialiser.
- **Severity:** WARNING, Heuristic.
- **Unknown:** none.

### LNT-055
- **Trigger:** in user code, `<` against a constant zero, or `<=` against a signed constant -1, where the left operand is a direct unsigned subtraction `a - b`.
- **Exempt:** signed differences; non-constant right operands and decremented variables are not tracked (constants outside a plain identifier's range are LNT-038).
- **Severity:** WARNING, Established.
- **Unknown:** none.

### LNT-056
- **Trigger:** (a) slang option unsized-concat (an unsized literal inside a concatenation or replication); (b) native: in user code, (i) a constant concatenation or replication on the right-hand side of a non-compound assignment whose width differs from the target width; (ii) a sized literal inside a concatenation or replication whose digits need more bits than its size.
- **Exempt:** non-constant concatenations (i); unsized literals (ii) (covered by (a)).
- **Severity:** (a) slang's severity; (b) WARNING, Established.
- **Unknown:** (ii) digits not in one source buffer or undecidable: WARNING, Unresolved ("could not be checked").

### LNT-057
- **Trigger:** in user code, a non-compound assignment or an input or output port connection whose needed source width is larger than the target width (for an output port, the port width is larger than the actual's width), where the declared type of either side names a parameter or type parameter in its scope chain. The needed width of `+`, `-` and bitwise right-hand sides is the widest operand.
- **Exempt:** compound assignments; InOut and Ref ports; a side whose type is a typedef or nested type is not classified as parameter dependent (see Unknown).
- **Severity:** WARNING, Heuristic.
- **Unknown:** the declared type text is not in one buffer: WARNING, Unresolved ("could not determine parameter dependence"); widths through a typedef or nested type are not classified.

### LNT-058
- **Trigger:** a sequential block with an identified asynchronous reset whose reset test in the first if / else-if chain has a polarity that runs the reset branch when the control is in its inactive level: a negedge control with the reset branch on high, or a posedge control with the reset branch on low. Also a block with three or more edge controls in which all but one are tested: each tested control whose test polarity mismatches its edge.
- **Exempt:** two-edge blocks whose reset is chosen by name (LNT-069); tests with no operand that tests the control directly.
- **Severity:** ERROR, Established.
- **Unknown:** none.

### LNT-059
- **Trigger:** slang options unused-import, unused-wildcard-import, unused-package-var, unused-package-typedef, unused-package-parameter, unused-package-type-parameter, unused-package-assertion-decl, unused-package-subroutine, shadow-value, shadow-hierarchy, shadow-property, dup-import.
- **Severity:** slang's severity (WARNING), Established.
- **Unknown:** none: decided by slang.

### LNT-060
- **Trigger:** a module-level variable with a blocking assignment in a clocked process and a read in another user procedure (any procedure other than initial or final) or in a user continuous assignment; one finding per variable, at its first such read. A select of a variable resolves to its base symbol, and every operand of a concatenation target is a target. Plus slang options multi-write and read-write (expression evaluation order), mapped to LNT-060.
- **Exempt:** nonblocking writes; reads in the writing process; initial and final procedures.
- **Severity:** WARNING, Established (native); slang's severity for the slang options.
- **Unknown:** reads and writes inside the bodies of user subroutines are not followed.

### LNT-061
- **Trigger:** in user code, a system subroutine call that is not on the synthesizable list ($clog2, $bits, $signed, $unsigned, $size, $high, $low, $left, $right, $increment, $dimensions, $countones, $onehot, $onehot0, $isunknown, $typename, $cast) or LNT-061.allow; elaboration-time $fatal, $error, $warning and $info. Plus slang option unknown-sys-name.
- **Exempt:** calls on the synthesizable list and on the LNT-061.allow option.
- **Severity:** WARNING, Established, for known simulation calls; INFO inside final blocks or assertions; INFO, Established, for elaboration-time messages.
- **Unknown:** unrecognised system name: WARNING, Unresolved ("not classified as synthesizable"); slang's unknown-sys-name at slang's severity.

### LNT-062
- **Trigger:** a nonblocking assignment whose target is a module-level variable (or a select of one; every operand of a concatenation target is a target) inside a combinational procedure: `always_comb`, `always @*`, or `always @(a or b ...)` without edges.
- **Exempt:** block-local temporaries; edge-triggered, latch and initial/final procedures; blocking assignments.
- **Severity:** WARNING, Established.
- **Unknown:** none (decided by the AST).

### LNT-063
- **Trigger:** a module-level variable (or a select of one; every operand of a concatenation target is a target) that is the target of at least one blocking and at least one nonblocking assignment in user code, in any procedure kind (including initial and final); reported once per variable at its first assignment.
- **Exempt:** block-local temporaries; continuous assignments and nets (not procedural targets in this check).
- **Severity:** WARNING, Established.
- **Unknown:** none (decided by the AST).

### LNT-064
- **Trigger:** an item of a `casez`, `casex` or `case inside` statement in user code whose constant value has every bit unknown (X or Z), so that it matches every value (wildcard item) and shadows the later items.
- **Exempt:** items that are not constants (skipped silently); items with at least one known bit; plain `case` statements.
- **Severity:** WARNING, Established.
- **Unknown:** none emitted for a non-constant item (see Exempt).

### LNT-065
- **Trigger:** slang options case-default, case-incomplete, case-none, case-not-wildcard, case-wildcard-2state, case-too-complex; slang codes MultipleDefaultCases, CaseStatementEmpty.
- **Severity:** slang's severity (WARNING), Established.
- **Unknown:** none: decided by slang.

### LNT-066
- **Trigger:** a node with an X or Z constant driver (a 'z driver of a net is its normal release and is not a source), or an X or Z reset value of a register, from which dependency edges that are not async-control reach an edge marked as a condition.
- **Exempt:** 'z drivers of nets.
- **Severity:** WARNING, Heuristic.
- **Unknown:** flow-insensitive: another driver or an earlier override may mask the value (Heuristic).

### LNT-067
- **Trigger:** (a) the reset identification of LNT-020: a module-level variable assigned in the non-reset branch and not in the reset branch; (b) from the model's per-bit reset values: for each register (flops with an asynchronous reset, grouped by name without the bit index), the bits that no asynchronous reset branch assigns, reported when some bit of the register is assigned by a reset branch or some of its flops have two or more asynchronous controls.
- **Exempt:** (a) as LNT-020 (single-edge blocks, blocks with more than two edge controls, variables assigned in both branches); (b) a register none of whose bits any reset branch assigns (left to (a)); a bit assigned a non-constant value in a reset branch counts as assigned (LNT-070).
- **Severity:** (a) WARNING, Established, or Heuristic when the reset is chosen by name or by a compound test; (b) WARNING, Established, one finding per register.
- **Unknown:** (a) name-chosen resets and compound tests are reported as Heuristic (above). (b) none: the bit values come from the model.

### LNT-068
- **Trigger:** an `always_ff` block in user code that assigns a variable and has no asynchronous control and no reset-named signal (names containing rst, reset, clr or clear) in its first if, inside an instance body where at least one other `always_ff` block has a reset.
- **Exempt:** instance bodies where no `always_ff` block resets.
- **Severity:** INFO, Heuristic.
- **Unknown:** none.

### LNT-069
- **Trigger:** a sequential block with at least two distinct edge controls in which fewer than all-but-one of the distinct controls are tested by the first if / else-if chain; or a block with three or more edge controls where not all but one are tested.
- **Exempt:** blocks with a single edge control; blocks in which all but one control are tested.
- **Severity:** WARNING, Heuristic.
- **Unknown:** name-chosen reset not tested by the first if: reported under the first condition above (Heuristic).

### LNT-070
- **Trigger:** a sequential block with an identified asynchronous reset whose reset branch assigns a right-hand side that `evalConst` does not evaluate to a constant. With one asynchronous control, the reset branch is the arm of the first if (or else-if chain) that a direct test of the reset with matching polarity selects. With three or more asynchronous controls, all but one tested by the first chain: the true-branch assignments of each if or else-if whose condition tests an asynchronous control.
- **Exempt:** with one control, reset branches not decided by a direct test (name-chosen reset or compound test) and blocks with a polarity mismatch (LNT-058); blocks where not all but one control are tested (LNT-069). With several controls, polarity is not checked.
- **Severity:** WARNING, Established; WARNING, Unresolved, when the right-hand side contains a user function call.
- **Unknown:** user function call in the right-hand side: Unresolved (above).

### LNT-071
- **Trigger:** every case statement that LNT-029 examines (an enum-typed selector of a type assigned in clocked code).
- **Severity:** WARNING, Heuristic, with proof state NOT_ATTEMPTED: reachability and liveness of every state are unproved.
- **Unknown:** always NOT_ATTEMPTED: reachability of every state and absence of deadlock are not proven here.

### LNT-072
- **Trigger:** on the shared model, a reset net (the traced root of a register reset) that reaches the combinational logic of a clock domain root.
- **Exempt:** async-control edges (reset to register clear or set).
- **Severity:** WARNING, Established.
- **Unknown:** paths end at black-box roots and are not followed (as LNT-027).

### LNT-073
- **Trigger:** a non-compound assignment in user code whose right-hand side is an integer literal (after implicit-conversion removal), whose target is narrower than the literal, and whose stored value changes under both the target's signedness and the literal's signedness.
- **Exempt:** values that read back unchanged under either signedness (for example -1 into a 4-bit unsigned target keeps its all-ones pattern); compound assignments; literals with x or z bits.
- **Severity:** WARNING, Established.
- **Unknown:** none.

### LNT-074
- **Trigger:** a comparison (`==`, `!=`, `===`, `!==`, `==?`, `!=?`, `<`, `<=`, `>`, `>=`) in user code with an unsized integer literal on one side and an operand wider than 32 bits on the other.
- **Exempt:** sized literals; fill literals.
- **Severity:** INFO, Heuristic.
- **Unknown:** none.

## Crosswalk and licences

Reference projects were studied for rule inventories only; no source code from any of them is included (SVerity reimplements every check).

| Source (licence) | Reference rules → SVerity |
|---|---|
| slang-tidy (MIT) | AlwaysCombNonBlocking, AlwaysFFBlocking → LNT-015/014; NoCaseX → LNT-016; NoLatchesOnDesign → LNT-013; UndrivenRange → LNT-008; OnlyAssignedOnReset, RegisterHasNoReset, AlwaysFFAssignmentOutsideConditional → LNT-020; UnusedSensitiveSignal → LNT-045; StructFieldsInSameProcess → LNT-041; XilinxDoNotCareValues → LNT-039; naming/ANSI/generate-style checks → STY; NoOldAlwaysSyntax → STY-007. Excluded: LoopBeforeResetCheck, CastSignedIndex (no stable semantics), UVM/assertion/constraint checks (out of scope) |
| slang-netlist (MIT) | bit-level netlist dependency graph and combinational-loop reports → `src/domains/model` and LNT-012 (design concept only) |
| Verible (Apache-2.0) | always-comb(-blocking), always-ff-non-blocking → LNT-015/045; case-missing-default → LNT-016; invalid-system-task-function → LNT-061; truncated-numeric-literal, undersized-binary-literal → LNT-026/056; forbid-defparam, naming, formatting → STY. Excluded: UVM macros, filename and `endif` comment rules |
| Verilator (LGPL-3.0 / Artistic-2.0) | WIDTH* → LNT-004; LATCH → LNT-013; COMBDLY/BLKSEQ/BLKANDNBLK → LNT-015; ALWCOMBORDER → LNT-044; CASEINCOMPLETE/CASEOVERLAP/CASEX/CASEWITHX → LNT-016/032/040; CMPCONST/UNSIGNED → LNT-038; MULTIDRIVEN → LNT-009; UNDRIVEN, UNUSED* → LNT-008/011; UNOPTFLAT → LNT-012; PIN* → LNT-003/010; SELRANGE → LNT-006; ASSIGNIN → LNT-050; delay and non-synth constructs → LNT-019. Excluded: simulator-performance and C++ naming warnings (UNOPT*, IMPERFECTSCH, SYMRSVDWORD) |

Retired IDs (merged, see `docs/SVerity_RFC.md` §9): LNT-021 → RDC-028, LNT-023 → LNT-003/057, LNT-028 → CST-005, LNT-030 → STY-*, LNT-043 → LNT-025, LNT-046 → LNT-047, LNT-052 (dropped: an inferred enable has no defect semantics; the model exposes enables to CDC-028/039).
Not copied: Verilator documentation text. If code is ever taken from an MIT/Apache-2.0 project, keep its notice and record it here first.
