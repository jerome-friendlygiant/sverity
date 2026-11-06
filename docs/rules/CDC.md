# CDC - Clock-domain crossings and synchronizer/protocol structure

Titles must match `catalog.json` (a unit test checks this). Specification bullets are the contract each rule is tested against
(`tests/fixtures/<RULE-ID>/`, with mutations: `mut_*` is a defect injected into the neighbouring `good_*` design). Recognition is structural:
a recognised structure is INFO only when it states its trust model; the unproved functional obligation of a recognised protocol is a WARNING (CDC-024, proof NOT_ATTEMPTED), never "proven". Relationship UNKNOWN is never treated as safe.

**One crossing, one primary finding.** CDC-016, CDC-017 (counter or partly synchronised Gray), CDC-028, CDC-032 and CDC-003/004 can describe one crossing group. The most specific enabled rule, in that order CDC-017, CDC-032, CDC-028, CDC-016, CDC-004, CDC-003, is the finding; the others become `also_matches` evidence, the severity is the highest of them, and a description with Established confidence keeps the finding Established. Disabling the primary rule passes the crossing to the next one, so disabling one rule cannot hide it. Each enabled rule keeps its own decision: the finding takes the highest severity after overrides, and it is waived only when each has a waiver, or by its fingerprint, which names this exact finding.
Crossing severity: ERROR if the clocks are declared asynchronous, WARNING if UNKNOWN, INFO if exclusive.

| ID | Required check |
|---|---|
| `CDC-001` | Clock-domain inventory and sequential elements without an identified clock root |
| `CDC-002` | Clock root that is not a primary input (logic-generated, gated through a black box, cyclic or undriven) |
| `CDC-003` | Asynchronous single-bit flop-to-flop crossing without recognized synchronization |
| `CDC-004` | Asynchronous multi-bit crossing without recognized safe protocol |
| `CDC-005` | Synchroniser shorter than the minimum depth |
| `CDC-006` | Combinational logic between synchronizer stages |
| `CDC-007` | Synchroniser stage before the required depth feeds anything but the next stage |
| `CDC-008` | Combinational, glitch-prone or unregistered source before synchronizer |
| `CDC-009` | Individually synchronized bus bits / incoherent capture |
| `CDC-010` | Destination cone combines two or more asynchronous influences |
| `CDC-012` | Missing/incorrect synchronization attributes or recognized primitive metadata |
| `CDC-013` | Pulse may be shorter than the destination capture window |
| `CDC-014` | Toggle/event-transfer synchronizer malformed |
| `CDC-015` | Handshake request/acknowledge structural mismatch |
| `CDC-016` | Unguarded multi-bit payload crossing despite synchronised control |
| `CDC-017` | Async FIFO Gray pointer sync chain or pointer crossing issue |
| `CDC-018` | Gray-code multi-bit crossing without trustworthy source stability/skew constraints |
| `CDC-019` | Clock multiplexer or combiner not recognised as glitch-free |
| `CDC-020` | Clock-gate enable launched in another clock domain |
| `CDC-023` | Register captures a value that may be driven by an unmodelled black box |
| `CDC-024` | Protocol functional-correctness obligation for a recognised FIFO, handshake or toggle structure |
| `CDC-026` | Synchronizer stage clock/reset mismatch |
| `CDC-028` | Clock-enable based unsafe data crossing |
| `CDC-029` | Asynchronous top-level input without CDC contract |
| `CDC-030` | create_generated_clock relationship assumed by constraints, not derived from the design |
| `CDC-031` | Clock multiplexer select not synchronous to the clocks it switches |
| `CDC-032` | Data/control bundle coherence after synchronization |
| `CDC-034` | Gray pointer pair with a pointer register that has no asynchronous reset |
| `CDC-035` | Memory written and read in different clock domains |
| `CDC-037` | Clock group relationship contradiction with observed synchronizers |
| `CDC-038` | Multi-cycle/falsely cut paths do not establish CDC safety |
| `CDC-039` | Payload qualification and data-stability obligations |
| `CDC-040` | Synchroniser recognised from an annotation or a library description |
| `CDC-041` | Reset-dependent CDC protocol activation / release |
| `CDC-043` | Mixed positive/negative edge domains |
| `CDC-044` | Asynchronous serial/event stream crossing assumptions |
| `CDC-045` | Clock gated by logic without a recognised enable latch |
| `CDC-046` | Black-box input fed from another clock domain |
| `CDC-047` | Launch bit synchronised more than once into one clock domain |

## Specification

Terms: a crossing group is a (source signal, destination signal) pair; its width is its number of destination bits; its relation is the clock relation of the two domains (Same, Async, Exclusive, Unknown). Same groups are not crossings. A chain starts at destination register d and follows non-blocking same-domain registers that are each fed only by the previous stage; depth counts them (a library synchroniser counts its declared stages). Depth >= 2 with no link logic is a recognised synchroniser for the qualifier, protocol and input rules; the crossing classes compare depth with CDC-005.min_stages (default 2, never below 2).

### CDC-001
- **Trigger:** each sequential block with a register bit whose clock is not identified (not exactly one clock edge event once the events tested by the top if-chain are removed), each `always_latch` procedure (ProcKind Latch), and one INFO per clock domain with its register-bit count.
- **Severity:** WARNING Unresolved for unidentified blocks and latches; INFO Informational per domain (proof NOT_APPLICABLE).
- **Unknown:** unresolved clock also records CST-010 `unresolved-clock`; scope clock-extraction INCOMPLETE. `always @*` latches are procedures of kind Comb and are not reported here.

### CDC-002
- **Trigger:** each clock domain whose root (traced through exact aliases) is not a top-level input port: logic-generated, black-box output, cyclic or undriven. A clock gated through an unmodelled black box also gets CDC-002.
- **Severity:** WARNING Unresolved.
- **Unknown:** the domain's relation to other clocks stays UNKNOWN (CST-004 per pair); scope clock-extraction INCOMPLETE.

### CDC-003
- **Trigger:** a single-bit crossing group (s -> d, relation not Same) where d starts no chain of depth >= 2 without link logic (unsynchronised) and d is not a qualified payload (CDC-039).
- **Exempt:** qualified payloads and undecided capture qualification (CDC-039). A recognised synchroniser (depth >= min_stages, no link logic, first stage without fan-out) is INFO, not a finding class change.
- **Severity:** ERROR Established when Async; WARNING Unresolved when Unknown; INFO Informational when Exclusive or recognised (proof NOT_ATTEMPTED). This is the severity convention used by "as CDC-003" below.
- **Unknown:** Unknown relation is WARNING Unresolved, never INFO. An unidentified clock on either side emits no CDC-003; the path is counted as CST-004 (scope clock-relationships INCOMPLETE).

### CDC-004
- **Trigger:** a multi-bit crossing group (width >= 2, relation not Same) with at least one destination bit that starts no chain of depth >= 2 without link logic, and that is not a qualified payload (CDC-039). A wide symbol (more than 16384 bits, tracked as one node) in a crossing is reported here without classification.
- **Exempt:** qualified payloads and undecided capture qualification (CDC-039). A group whose bits are all synchronised is decided by CDC-009, CDC-017 and CDC-018; a partly synchronised Gray pointer is reported here as well as by CDC-017.
- **Severity:** as CDC-003. Wide symbol: WARNING Unresolved, or ERROR Unresolved when Async and no next-stage register exists; INFO when Exclusive.
- **Unknown:** wide symbol: scope graph-precision INCOMPLETE and CST-010 wide-symbol fallback; Unknown relation as CDC-003.

### CDC-005
- **Trigger:** a crossing group (relation not Same) with a destination bit whose chain has depth >= 2, no link logic, and depth < CDC-005.min_stages.
- **Severity:** as CDC-003.
- **Unknown:** Unknown relation as CDC-003; none otherwise.

### CDC-006
- **Trigger:** a crossing group (relation not Same) whose chain from d meets logic (a non-exact Seq edge) between any two consecutive stages; the chain stops there, so stages other than 1 and 2 are checked too.
- **Severity:** as CDC-003.
- **Unknown:** Unknown relation as CDC-003; none otherwise.

### CDC-007
- **Trigger:** a crossing group (relation not Same) or primary input whose chain has a stage short of `CDC-005.min_stages` that drives anything besides the next stage: another register, logic, a top-level output, or a clock or asynchronous-reset use, including capture by a register in another clock domain. With the default of two stages this means the first stage.
- **Exempt:** a stage that has reached `CDC-005.min_stages`. It is an output of the synchroniser, whatever registers follow it, so a pipeline register there and logic beside it are ordinary use.
- **Severity:** as CDC-003.
- **Unknown:** Unknown relation as CDC-003; none otherwise.

### CDC-008
- **Trigger:** a crossing group (relation not Same, not a wide symbol) whose capture cone has a non-exact edge from the launch register: any operator, or a condition such as an enable.
- **Exempt:** none; relation Exclusive gives INFO instead of WARNING.
- **Severity:** WARNING Established; WARNING Unresolved when Unknown; INFO Informational when Exclusive.
- **Unknown:** a cone through an unmodelled black box also gets CDC-023; wide symbols are classified by CDC-004 only.

### CDC-009
- **Trigger:** a multi-bit crossing group whose destination bits all start chains of depth >= min_stages without link logic and which is neither a Gray pointer, a counter (CDC-017) nor a qualified payload (CDC-039); also bits of one source signal captured into several destination groups or registers, each of depth >= 2.
- **Exempt:** Gray pointers (CDC-018), counters (CDC-017), qualified payloads (CDC-039), relation Same.
- **Severity:** WARNING Established, INFO Informational when Exclusive; WARNING Unresolved when Unknown; proof NOT_ATTEMPTED.
- **Unknown:** Unknown relation as above; none otherwise.

### CDC-010
- **Trigger:** a register d whose fan-in cone holds two or more asynchronous influences: outputs of recognised synchronisers in d's own domain (also one register stage further), and sources from another domain (relation not Same) that are not gated by a synchronised qualifier. Reported once per destination name and reconvergence kind (unsynchronised, multi-domain or same-domain).
- **Exempt:** cross-domain sources captured under a synchronised qualifier (CDC-039 pattern) are not counted as raw influences.
- **Severity:** WARNING Heuristic (proof NOT_ATTEMPTED), whatever the relation.
- **Unknown:** none. Primary inputs combined before one register are reported here too (see CDC-029).

### CDC-012
- **Trigger:** a single-bit synchronised crossing (depth >= 2, no link logic) whose stage registers carry ASYNC_REG, sverity_sync or cdc_sync (case-insensitive) on some but not all stages (any profile), or on none of them under profile fpga.
- **Exempt:** no attribute on any stage under the asic or generic profile.
- **Severity:** WARNING Established.
- **Unknown:** none; attributes are read from the RTL symbols only.

### CDC-013
- **Trigger:** a single-bit synchronised crossing (depth >= 2, relation not Exclusive) whose clock periods are not both known from create_clock (always reported), or whose source period is < 1.5 x the destination period, or whose source register is computed from logic (event-like) while both periods are known.
- **Exempt:** relation Exclusive.
- **Severity:** WARNING Established for the fast-to-slow case; WARNING Heuristic for an event-like source with known periods; WARNING Unresolved when a period is unknown. Proof NOT_ATTEMPTED throughout.
- **Unknown:** a missing create_clock period gives WARNING Unresolved for each such crossing; no pulse-width analysis is done.

### CDC-014
- **Trigger:** a single-bit synchronised crossing (depth >= 2) whose source register is a toggle (its next value depends on its own value through logic). WARNING when logic lies between the toggle register and the synchroniser, or when no edge detector is found; an edge detector is logic that combines a non-first stage with the stage after it.
- **Severity:** WARNING Established (logic before the toggle); WARNING Heuristic (no edge detector); INFO Heuristic with a trust model when an edge detector is found (the protocol obligation is CDC-024).
- **Unknown:** toggle and detector classification is structural (Heuristic).

### CDC-015
- **Trigger:** two single-bit synchronised crossings (depth >= 2) in one scope that run in opposite directions between the same two domains, where each source register's cone contains a stage of the other crossing's synchroniser.
- **Severity:** INFO Heuristic with a trust model when both legs depend on each other (handshake; the protocol obligation is CDC-024); WARNING Heuristic when only one leg depends on the other (mismatch).
- **Unknown:** dependence is a structural cone test (Heuristic).

### CDC-016
- **Trigger:** an unsynchronised, unqualified multi-bit payload crossing (width >= 2, depth < 2, no link logic) from domain X to Y in a scope (same destination process instance) that also contains a recognised synchroniser (depth >= 2) from X to Y for another source signal.
- **Exempt:** payloads qualified by a synchronised control (CDC-039 pattern); a single bit (CDC-003 or CDC-028).
- **Severity:** as CDC-003.
- **Unknown:** Unknown relation as CDC-003; none otherwise.

### CDC-017
- **Trigger:** a multi-bit crossing group (width >= 2, relation not Same) whose source is a binary counter (some source bit's next value depends on another bit of the bus: carry), or a Gray-coded pointer (every source bit is a Gray conversion of a counter) with at least one destination bit that is not a clean chain of depth >= min_stages without link logic.
- **Exempt:** Gray pointers whose bits are all synchronised (CDC-018 decides them).
- **Severity:** counter: ERROR Heuristic (Async), INFO Heuristic (Exclusive), WARNING Heuristic otherwise. Gray not fully synchronised: as CDC-003. An unanalysed source function gives WARNING Unresolved and a proof obligation.
- **Unknown:** counter detection is heuristic; Unknown relation gives WARNING Unresolved for the Gray case. User-function output remains unresolved unless its single pure body is proved to implement the supported Gray conversion; scope is INCOMPLETE.

### CDC-018
- **Trigger:** a Gray-coded crossing (CDC-017 detection) whose destination bits all start chains of depth >= min_stages without link logic. WARNING when no SDC set_max_delay matches the first source bit and the first destination register; INFO when one matches.
- **Severity:** WARNING Unresolved (INFO Unresolved when Exclusive, with a trust model), proof NOT_ATTEMPTED, when no max-delay; INFO Heuristic with a trust model (the constraint is trusted, its value is not checked) when a max-delay matches.
- **Unknown:** bus skew and the Gray property are assumed, never proven; the max-delay value is not checked against the clock periods.

### CDC-019
- **Trigger:** a clock domain whose root is a logic net whose combinational cone reaches two or more clock leaves (clock roots, SDC-declared ports, or top-level inputs named clk or clock).
- **Exempt:** none; no glitch-free clock mux cell is recognised.
- **Severity:** WARNING Unresolved.
- **Unknown:** always WARNING Unresolved; scope clock-extraction INCOMPLETE.

### CDC-020
- **Trigger:** a clock gated by combinational logic (exactly one clock leaf, enable from registers or inputs) whose enable register is in a domain whose relation to the clock leaf is not Same, or whose enable is a primary input, or whose enable register has no identified clock; also a library clock_gate instance whose enable comes from a register in another domain than its clock.
- **Exempt:** enable registers in a Same domain. Latch-based gates are not exempt here (see CDC-045).
- **Severity:** ERROR Established (Async); INFO Informational (Exclusive); WARNING Established (other known relation); WARNING Unresolved (Unknown, primary-input enable, unidentified enable or clock).
- **Unknown:** primary-input enable and unidentified enable or clock give WARNING Unresolved; a clock cone through an unmodelled black box also gets CDC-002.

### CDC-023
- **Trigger:** a register bit whose fan-in cone reaches a net connected to an unmodelled black box (undeclared instance, all connected nets) or to an output of a declared --library black box. Library RAM read data is not included.
- **Severity:** WARNING Unresolved, one per capturing signal.
- **Unknown:** scope blackboxes INCOMPLETE; whether the path crosses a clock domain stays unresolved.

### CDC-024
- **Trigger:** one obligation per recognised structure: a Gray FIFO pointer pair (CDC-034), a handshake (CDC-015 with mutual dependence) or a toggle (CDC-014 with an edge detector) that is not a leg of a recognised handshake. A two-phase handshake is one structure; its obligation lists the toggles it covers.
- **Severity:** WARNING Heuristic, proof NOT_ATTEMPTED: the functional correctness of a recognised protocol is unproved.
- **Unknown:** never reported as verified; the message states that functional correctness is not verified.

### CDC-026
- **Trigger:** a crossing group (relation not Same) whose chain stages differ from the previous stage in clock edge, or whose stages have different asynchronous reset roots. Only stages through `CDC-005.min_stages` are compared: a register after them is not part of the synchroniser.
- **Severity:** as CDC-003.
- **Unknown:** Unknown relation as CDC-003; none otherwise.

### CDC-028
- **Trigger:** a crossing group with at least one destination bit that is unsynchronised and unqualified (depth < 2, no link logic) whose capture register has a clock-enable (control) input.
- **Exempt:** qualified payloads and undecided capture qualification (CDC-039).
- **Severity:** as CDC-003.
- **Unknown:** Unknown relation as CDC-003; undecided capture-control necessity is reported by CDC-039.

### CDC-029
- **Trigger:** a top-level input bit captured by registers of clock domain D. Declared synchronous when a set_input_delay -clock C names the input and C has relation Same to D; recognised when every capturing register starts a chain of depth >= 2 without link logic.
- **Exempt:** none. A declared synchronous input is INFO and skips the structure checks below.
- **Severity:** INFO Heuristic when declared synchronous or recognised; WARNING Unresolved otherwise.
- **Unknown:** no matching set_input_delay and no recognised synchroniser gives WARNING Unresolved; on such an input CDC-006, CDC-007, CDC-008, CDC-009 and CDC-010 are also evaluated (WARNING, Established or Heuristic) as for a register crossing.

### CDC-030
- **Trigger:** each clock domain whose SDC clock is declared with create_generated_clock.
- **Severity:** WARNING Unresolved.
- **Unknown:** crossings between the generated clock and its master stay UNKNOWN until a clock group relates them; scope clock-relationships INCOMPLETE.

### CDC-031
- **Trigger:** a clock mux (CDC-019 shape, two or more clock leaves) where a select register is not in the Same domain as a switched clock, a select has no identified clock, a select is a primary input, or the output clock's relation to a leaf is Unknown.
- **Severity:** WARNING Unresolved.
- **Unknown:** the unknown-relation case is itself a finding; scope clock-extraction INCOMPLETE (as CDC-019).

### CDC-032
- **Trigger:** (a) a multi-bit crossing group whose destination bits have chains of different depth (depth >= 2, no link logic), or some bits chain-synchronised and some not; (b) bits of one source signal captured into separate destination registers with different chain depths (a link-logic bit counts as depth 1).
- **Severity:** (a) as CDC-003; (b) ERROR Established when Async, INFO when Exclusive, otherwise WARNING Established.
- **Unknown:** Unknown relation gives Unresolved confidence (WARNING).

### CDC-034
- **Trigger:** two Gray-coded crossing groups (CDC-017 detection) in opposite directions between the same two domains in one scope (same destination process instance).
- **Severity:** WARNING Heuristic when any pointer bit on either side has no asynchronous reset, or a register in the fan-in cone of a pointer bit (its binary counter) has none; INFO Heuristic otherwise (no proof state: the unproved FIFO obligations are CDC-024 and RDC-023, emitted for every pair).
- **Unknown:** heuristic pair detection; pointers reset from different reset roots are not reported here (RDC-023 does).

### CDC-035
- **Trigger:** (a) a crossing group (relation not Same) whose source or destination signal is an unpacked-array memory, wide memories included; (b) a library RAM (--library kind ram) whose write clock domain and read clock domain differ with relation not Same; (c) each port of a library RAM with a data-in, write-enable, address or data-out role and with nodes: an input port whose fan-in cone reaches registers or primary inputs, and a data-out port whose value is captured by registers through combinational logic. Each register crossing is checked between its own domain and the port's reference clock domain: the domain of the clock port named by the port's library `clock` field; without that field, data-in and write-enable use the write clock, data-out uses the read clock, and an address port has none.
- **Exempt:** relation Same (no finding); relation Exclusive gives INFO.
- **Severity:** (a), (b): WARNING Unresolved (proof NOT_ATTEMPTED), any relation other than Exclusive; INFO Unresolved when Exclusive. (c): per register crossing ERROR Established when Async, INFO Established when Exclusive, WARNING Established otherwise, WARNING Unresolved when the relation is Unknown; WARNING Unresolved for a register with an unidentified clock, a port with no reference clock, and a primary input in a port cone.
- **Unknown:** library RAM with a write or read clock that does not resolve: WARNING Unresolved, scope clock-extraction INCOMPLETE; (c) an unresolved reference or register clock: WARNING Unresolved, scope clock-extraction INCOMPLETE; a primary input in a port cone: WARNING Unresolved, scope constraints INCOMPLETE; vendor read-during-write semantics are not modelled.

### CDC-037
- **Trigger:** (a) a crossing group whose clocks are declared exclusive (set_clock_groups -logically_exclusive or -physically_exclusive) and whose destination starts a chain of depth >= 2; (b) two distinct clock roots that SDC gives the same clock name, so paths between them are treated as synchronous.
- **Severity:** (a) WARNING Established; (b) WARNING Unresolved.
- **Unknown:** (b) scope clock-relationships INCOMPLETE.

### CDC-038
- **Trigger:** a crossing group (relation not Same) covered by an SDC set_false_path or set_multicycle_path whose -from and -to match the source and destination registers (a -through-only exception matches on its -through names).
- **Exempt:** set_max_delay and set_min_delay exceptions (the path stays timed).
- **Severity:** WARNING Established; the message states whether a synchroniser is present.
- **Unknown:** -through is not path-evaluated: when -from or -to is present it is ignored, so the match uses -from and -to names only.

### CDC-039
- **Trigger:** an unsynchronised payload crossing whose capture qualification is either proven necessary or undecided. Proven necessary means every data-moving write requires a control synchronised from the launch domain into the capture domain; local conjunctions may restrict capture further. A concrete bypass or absence of matching control is instead reported by CDC-003/004 and CDC-028.
- **Severity:** WARNING Informational for proven necessity, retaining the unproved stability/timing obligation. WARNING Unresolved for undecided necessity. Both carry proof NOT_ATTEMPTED.
- **Unknown:** state-held validity, a qualifying chain shorter than the required depth, opaque calls, unsupported Boolean semantics, cyclic logic or proof/walk limits prevent a necessity claim; the path stays visible with an explicit proof obligation and INCOMPLETE scope. Undecided messages do not claim the qualifier pattern. Neither result proves data stability.

### CDC-040
- **Trigger:** (a) a module annotated (* sverity_sync *) or (* cdc_sync *), checked for a recognised synchroniser (depth >= 2) inside its instance path; (b) every declared --library cell instance of kind synchronizer, reported with its stage count (flop, clock_gate, ram and blackbox cells are described by their own rules).
- **Severity:** (a) WARNING Established when no recognised synchroniser is inside, INFO Heuristic when one is (proof NOT_ATTEMPTED); (b) INFO Heuristic, proof NOT_ATTEMPTED.
- **Unknown:** a malformed --library file or cell entry gives CST-018 ERROR and the cell is not registered, so its instance is walked as an ordinary module; library declarations are trusted, not verified.

### CDC-041
- **Trigger:** a synchronised crossing (single-bit with depth >= 2, or multi-bit whose first destination bit starts a chain of depth >= 2 without link logic; not a memory) whose source register and synchroniser output stage (the stage reaching the required depth, not a follower) have different asynchronous reset roots, or whose source is reset and the last stage is not.
- **Severity:** WARNING Heuristic for different reset roots (proof NOT_ATTEMPTED); INFO Heuristic for a source-only reset.
- **Unknown:** heuristic: reset roots are compared structurally, not by reset values or timing.

### CDC-043
- **Trigger:** (a) a register-to-register path inside one clock domain (through the fan-in cone) between a register on one clock edge and a register on the opposite edge; (b) a register triggered on both clock edges.
- **Severity:** (a) WARNING Heuristic; (b) WARNING Unresolved.
- **Unknown:** (b) dual-edge behaviour is not modelled; scope clock-relationships INCOMPLETE.

### CDC-044
- **Trigger:** a top-level input bit (declared or not) whose capturing registers all start chains of depth >= 2 without link logic, and whose first chain is an edge detector (a stage compared with the next one as data by XOR/XNOR, `!=` or `==`, or by AND with exactly one of them inverted, whether in a continuous assignment or in a clocked block) or has depth >= 4.
- **Severity:** WARNING Heuristic, proof NOT_ATTEMPTED (oversampling is unproved).
- **Unknown:** none.

### CDC-045
- **Trigger:** (a) a clock gated by combinational logic (one clock leaf) whose enable reaches the gate from a register or a primary input, with no latch that is transparent while the clock is low (an always_latch with a negated clock condition) on any enable path; (b) a library clock_gate whose declaration has enable_latched false; (c) a clock gated through such a latch, or a library clock_gate with enable_latched true.
- **Exempt:** latch-based gates (c) are INFO, not findings.
- **Severity:** (a) WARNING Heuristic; (b) WARNING Established; (c) INFO Heuristic with a trust model (the latch pattern is recognised; its phase and timing are not verified).
- **Unknown:** a primary-input enable is also reported by CDC-020 (WARNING Unresolved).

### CDC-046
- **Trigger:** (a) declared box (--library kind blackbox): each input port (not an output, clock or reset port) whose fan-in cone reaches registers or primary inputs, checked against its reference clock: the clock port named by the port's library `clock` field, else the box's only clock port; with several clock ports and no association the reference is unresolved. Each register whose domain has a relation to the reference other than Same is a crossing. (b) undeclared box: every connected net; a cone spanning two or more source domains, or clocks of two or more domains or SDC clocks whose relation is not Same entering the box, where a crossing inside it is not analysed. In both forms an input from a register with an unidentified clock always reports.
- **Exempt:** sources in the reference domain; relation Same; single-domain inputs of an undeclared box.
- **Severity:** (a) ERROR Established (Async), INFO Established (Exclusive), WARNING Established (other known relation) per crossing; WARNING Unresolved for an Unknown relation, an unresolved reference clock, an unidentified source clock, and a primary input. (b) WARNING Unresolved.
- **Unknown:** an undeclared box fed from one domain gets no CDC-046 (its nets are covered by CDC-023); an unresolved reference clock or a multi-domain undeclared box marks scope blackboxes INCOMPLETE; an unidentified source clock marks clock-extraction INCOMPLETE; a primary input marks constraints INCOMPLETE.

### CDC-047
- **Trigger:** one launch bit with recognised chains (depth >= 2, no link logic) into two or more destination symbols in one clock domain; one per launch symbol and set of destinations.
- **Exempt:** chains in different destination domains; one chain whose last stage fans out; a wide symbol (CDC-004 or CDC-035).
- **Severity:** WARNING Heuristic, proof NOT_ATTEMPTED; INFO when the clocks are declared exclusive.
- **Unknown:** Unknown relation gives WARNING Unresolved. Whether logic combines the copies is not established: the finding is the structure, not the glitch.

## Crosswalk and licences

The rule implementations are original; the reference-rule mappings below record design influences. Vendor rule lists were read from the public pages cited in the [roadmap](../ROADMAP.md#sources-and-provenance) on 2026-11-03 and are paraphrased. The AMD page only renders with JavaScript, so its list was read twice through an automated summary (2026-11-03 and 2026-11-05, with the same rule IDs and meanings), never as raw text: check the page before relying on one entry. **The mappings are closest intent, never equivalence:** no vendor tool has been run to compare.

| Source (licence) | Reference rules → SVerity |
|---|---|
| OpenCDC (Apache-2.0) | unsynchronized crossing → CDC-003; multi-bit → CDC-004; reconvergence → CDC-010; gated clock → CDC-020; muxed clock → CDC-019; comb between sync → CDC-006; daisy chain → CDC-007; reset-domain crossing → RDC-004/005; path truncation → CST-010; pulse/toggle → CDC-013/014; unknown propagation → LNT-040/054 |
| RTL Buddy CDC (BSD-3-Clause) | 001 → CDC-003; 002/022 → CDC-005; 003/006 → CDC-008; 004/019/020 → CDC-004/009/032; 005 → CDC-010; 008 clock as data → LNT-027; 009/013 → CDC-013/014; 010 clock glitch → CDC-020; 011 input → CDC-029; 012 → CDC-028/039; 014 → CDC-006; 015 → CDC-026/041; 016 → CDC-043; 017 latch → CDC-001; 018 → CDC-007; 021 → CST-011/CDC-002; 023 → CDC-019 |
| AMD Vivado UG906 (vendor) | CDC-1 → CDC-003; CDC-2 → CDC-012; CDC-3 → CDC-003 INFO with CDC-012; CDC-4 → CDC-004; CDC-5 → CDC-009 (attributes are not read on buses: CDC-012 is single-bit); CDC-6 → CDC-009; CDC-10 → CDC-008; CDC-12 → CDC-010; CDC-15, 16, 17 → CDC-028/039; CDC-18 → CDC-040. CDC-11 → CDC-047. Reset rules CDC-7, 8, 9: see `RDC.md`. **Excluded:** CDC-13, CDC-26 (below). The list has no CDC-14 or CDC-19 to CDC-25 |
| Altera Quartus Design Assistant 21.3 (vendor) | CDC-50001 → CDC-003 with CDC-008; CDC-50005 → CDC-032; CDC-50006 → CDC-004; CDC-50008 → CDC-009, CDC-017/018 for Gray; CDC-50011 → CDC-008; CDC-50012 → CDC-010. **Partial:** CDC-50002 → CST-004 (declared relationship only, not a max-delay value); CDC-50003, 50004 → CDC-039 obligation (skew and net-delay constraints are not checked); CDC-50007 → CDC-018 for Gray only. **Excluded:** the value checks of 50002, 50003, 50004, 50007 (below). **No such rule found:** CDC-50009 and 50010. The [21.3 rule list](https://resources.altera.com/quartushelp/21.3/da_rules/da-root-topic.htm) goes from 50008 to 50011, their 21.3 URLs return 404 and AN 919 does not list them (checked 2026-11-05). That list covers new and updated rules, so this does not prove they never existed in another release. **Excluded:** CDC-50101, 50102, 50103 (below) |

**Written exclusions.** Decided, not forgotten; each can be reopened with a new reason.

| Public check | Why there is no SVerity rule | What SVerity does instead |
|---|---|---|
| AMD CDC-13: crossing on a non-flop primitive | The check is about mapped primitives, which RTL does not have. | A crossing into a latch is CDC-001, into an unknown cell CDC-023/046, and into a declared cell follows its `--library` kind. Nothing is silently treated as a flop. |
| AMD CDC-26: possible LUTRAM read/write collision | Whether a memory becomes LUTRAM is an implementation choice, so it cannot be established from RTL. | CDC-035 reports every memory written and read in different domains, and LNT-049's read-during-write obligation stays open. |
| Altera CDC-50002, 50003, 50004, 50007: the values of `set_max_delay`, `set_max_skew`, `set_net_delay`, `set_data_delay` | Comparing a delay with a clock period is timing analysis, excluded by the [charter](../SVerity_RFC.md#1-product-charter). | CST-004 reports unknown relationships, CDC-039 and CDC-018 keep the stability and skew obligations, and CST-009 discloses any of these commands it cannot read. |
| Altera CDC-50101, 50102, 50103: synchroniser on an intra-clock false path, synchroniser after a control-qualified crossing, and an unsynchronised transfer forced to head a chain | They advise on Quartus settings (`-no_synchronizer`, chain length, `SYNCHRONIZER_IDENTIFICATION`), which SVerity does not read, and 50102 is about removing a redundant synchroniser rather than a defect. | An unsynchronised crossing is CDC-003; a same-clock transfer is not a crossing. |

Retired IDs: CDC-011 → CDC-010, CDC-021 → RDC-007, CDC-022 → CST-004 + CDC-038, CDC-025 → CDC-008, CDC-027 → CDC-007, CDC-033 → CDC-008 + CDC-018 (obligation: CDC-024), CDC-036 → CDC-010, CDC-042 → CDC-013. OpenCDC and RTL Buddy code may be reused under their licences if notices are kept; none is.


### Accuracy-case adaptations

Reduced and modified RTL under `tests/accuracy/cases/CDC/` exercises synchronizers, pulse and request/acknowledge transfers, asynchronous FIFOs and clock control. The upstream licences apply to these adaptations; each case retains its attribution and records the exact source path and changes in `label.json`. Whole upstream designs are not vendored. Licence texts and notices ship in [tests/accuracy/LICENSE](../../tests/accuracy/LICENSE).

| Source at pinned commit | Licence | Material taken |
|---|---|---|
| [pulp-platform/common_cells](https://github.com/pulp-platform/common_cells/tree/e73baaec2ca665cd80c3c384e9258e35242b829c) | SHL-0.51 | Reduced RTL fragments used as CDC test inputs and mutated lookalikes |
| [lowRISC/opentitan](https://github.com/lowRISC/opentitan/tree/92f3d03b0dcf6d5c1933a65d1497747f67d19f4c) | Apache-2.0 | Reduced RTL fragments used as CDC test inputs and mutated lookalikes |
| [alexforencich/verilog-axis](https://github.com/alexforencich/verilog-axis/tree/48ff7a7e2ef782cf778d47910cf85835c64b1bce) | MIT | Reduced RTL fragments used as CDC test inputs and mutated lookalikes |
