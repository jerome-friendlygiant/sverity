# CDC - Clock-domain crossings and synchronizer/protocol structure

Titles must match `catalog.json` (a unit test checks this). Specification bullets are the contract each rule is tested against
(`tests/fixtures/<RULE-ID>/`, with mutations: `mut_*` is a defect injected into the neighbouring `good_*` design). Recognition is structural:
a recognised protocol is INFO with its proof obligation (state NOT_ATTEMPTED), never "proven". Relationship UNKNOWN is never treated as safe.
Crossing severity: ERROR if the clocks are declared asynchronous, WARNING if UNKNOWN, INFO if exclusive.

| ID | Required check |
|---|---|
| `CDC-001` | Clock roots extracted for every sequential element |
| `CDC-002` | Generated/gated clock relationship unresolved |
| `CDC-003` | Asynchronous single-bit flop-to-flop crossing without recognized synchronization |
| `CDC-004` | Asynchronous multi-bit crossing without recognized safe protocol |
| `CDC-005` | Synchronizer depth too short or first stage used unsafely |
| `CDC-006` | Combinational logic between synchronizer stages |
| `CDC-007` | Synchronizer first-stage output fans out into functional logic |
| `CDC-008` | Combinational, glitch-prone or unregistered source before synchronizer |
| `CDC-009` | Individually synchronized bus bits / incoherent capture |
| `CDC-010` | Reconvergent paths after independent synchronization |
| `CDC-011` | Different-domain signals reconverging into one destination cone |
| `CDC-012` | Missing/incorrect synchronization attributes or recognized primitive metadata |
| `CDC-013` | Fast-to-slow pulse potentially missed |
| `CDC-014` | Toggle/event-transfer synchronizer malformed |
| `CDC-015` | Handshake request/acknowledge structural mismatch |
| `CDC-016` | Unguarded multi-bit payload crossing despite synchronised control |
| `CDC-017` | Async FIFO Gray pointer sync chain or pointer crossing issue |
| `CDC-018` | Gray-code multi-bit crossing without trustworthy source stability/skew constraints |
| `CDC-019` | Dynamic clock mux select not recognized as glitchless |
| `CDC-020` | Clock gating enable crossing / potentially glitchy gating |
| `CDC-021` | Async control/set/clear crossing used as a functional signal |
| `CDC-023` | Unrecognized macro/IP crossing boundary |
| `CDC-024` | Verify actual FIFO/handshake functional correctness |
| `CDC-026` | Synchronizer stage clock/reset mismatch |
| `CDC-027` | Synchronizer chain stage fanout and feedback violations |
| `CDC-028` | Clock-enable based unsafe data crossing |
| `CDC-029` | Asynchronous top-level input without CDC contract |
| `CDC-030` | Generated-clock relationship and phase assumptions |
| `CDC-031` | Clock mux selection CDC and output clock interaction |
| `CDC-032` | Data/control bundle coherence after synchronization |
| `CDC-033` | FIFO pointer source stability and Gray transition requirement |
| `CDC-034` | Asynchronous FIFO reset and pointer CDC interaction |
| `CDC-035` | Memory read/write cross-domain boundary |
| `CDC-036` | Combinational reconvergence before destination sample |
| `CDC-037` | Clock group relationship contradiction with observed synchronizers |
| `CDC-038` | Multi-cycle/falsely cut paths do not establish CDC safety |
| `CDC-039` | Multi-bit synchronizer coherency and skew contract |
| `CDC-040` | Dynamic or user-defined synchronizer primitive recognition |
| `CDC-041` | Reset-dependent CDC protocol activation / release |
| `CDC-042` | Pulse width guarantee absent for level/pulse event crossing |
| `CDC-043` | Mixed positive/negative edge domains |
| `CDC-044` | Asynchronous serial/event stream crossing assumptions |

## Specification

- **CDC-001** — every register bit gets a clock root (resolved edge event of its block); latches and blocks with several edge events are reported. Exempt: none. Severity: INFO per clock domain; WARNING (Unresolved) for unidentified clocks, latches. Unknown: scope clock-extraction INCOMPLETE.
- **CDC-002** — a clock root that is not a primary input (logic-generated, black box, cyclic, undriven). Exempt: none. Severity: WARNING (Unresolved). Unknown: relationship stays UNKNOWN.
- **CDC-003** — single-bit register-to-register crossing between clocks that are not declared Same; recognised = >= min_stages same-domain registers fed only by the previous stage. Exempt: relations declared exclusive (INFO); recognised synchroniser (INFO with trust model). Severity: ERROR when declared asynchronous, WARNING when UNKNOWN, INFO when exclusive/recognised. Unknown: UNKNOWN relationship -> WARNING Unresolved, never INFO.
- **CDC-004** — multi-bit crossing without a recognised scheme (unsynchronised bits). Exempt: recognised Gray pointers and qualified payloads (CDC-018/039). Severity: as CDC-003. Unknown: wide (>1024 bit) symbols are Unresolved WARNING and scope graph-precision INCOMPLETE.
- **CDC-005** — synchroniser shorter than CDC-005.min_stages (default 2). Exempt: none. Severity: as CDC-003. Unknown: n/a.
- **CDC-006** — combinational logic between stage 1 and 2 of a synchroniser. Exempt: none. Severity: as CDC-003. Unknown: n/a.
- **CDC-007** — the first stage of a synchroniser also feeds logic other than the next stage. Exempt: none. Severity: as CDC-003. Unknown: n/a.
- **CDC-008** — combinational logic (or a gate primitive) between the launch register and the capturing register; unregistered sources. Exempt: exclusive clocks (INFO). Severity: WARNING (INFO when exclusive). Unknown: source cone through black boxes is Unresolved (CDC-023).
- **CDC-009** — synchronised multi-bit group whose bits are synchronised independently and are neither Gray nor qualified. Exempt: Gray pointers (CDC-018), qualified payloads (CDC-039). Severity: WARNING (INFO when exclusive), proof state NOT_ATTEMPTED. Unknown: n/a.
- **CDC-010** — a register whose cone combines >= 2 independently synchronised signals from one source domain. Exempt: payload captured under a synchronised qualifier. Severity: WARNING (heuristic). Unknown: n/a.
- **CDC-011** — a register whose cone combines synchronised signals from >= 2 different source domains. Exempt: qualifier pattern. Severity: WARNING (heuristic). Unknown: n/a.
- **CDC-012** — fpga profile: a recognised synchroniser whose stages lack an ASYNC_REG-style attribute; any profile: some stages marked, some not. Exempt: asic/generic profile without any attribute. Severity: WARNING. Unknown: library-cell metadata cannot be read (profile format undefined).
- **CDC-013** — synchronised single-bit crossing from a clock with period < 1.5 x the destination period (periods from create_clock). Exempt: exclusive clocks. Severity: WARNING, proof state NOT_ATTEMPTED. Unknown: periods unknown -> no CDC-013; computed sources get CDC-042.
- **CDC-014** — toggle source (register with logic self-feedback) crossing: without a destination edge detector (XOR with a delayed copy) or with logic before the toggle flop. Exempt: none. Severity: WARNING; INFO + obligation when recognised. Unknown: heuristic classification (Confidence heuristic).
- **CDC-015** — single-bit synchronised crossings in opposite directions in one scope: both legs depend on each other = handshake (INFO + obligation); one-sided dependency = mismatch. Exempt: none. Severity: INFO / WARNING. Unknown: heuristic.
- **CDC-016** — unqualified payload crossing between two domains in a scope that also contains a synchronised control between the same domains. Exempt: payloads qualified by that control. Severity: as CDC-003. Unknown: n/a.
- **CDC-017** — multi-bit pointer crossing: binary counter (carry dependence between the bits), or Gray pointer without full per-bit synchronisation. Exempt: none. Severity: as CDC-003. Unknown: heuristic (counter detection).
- **CDC-018** — Gray-coded group (b ^ (b >> 1) idiom or adjacent-bit XOR) synchronised per bit: WARNING when no set_max_delay covers the path, INFO when one does (value not checked). Exempt: none. Severity: WARNING (Unresolved, proof NOT_ATTEMPTED) / INFO. Unknown: bus skew is never proven.
- **CDC-019** — a clock built from >= 2 clocks by combinational logic (mux/combine). Exempt: none (no glitch-free primitive library exists, so none is recognised). Severity: WARNING (Unresolved), scope clock-extraction INCOMPLETE. Unknown: n/a.
- **CDC-020** — a clock gated by logic: enable launched in another domain, gating without an enable latch, gating through a black box. Exempt: latch-based gating (INFO). Severity: ERROR (declared asynchronous), WARNING otherwise. Unknown: primary-input enable = Unresolved WARNING.
- **CDC-021** — a register output used both as functional data and as an asynchronous set/clear of registers in another clock domain. Exempt: pure resets (RDC-007). Severity: as CDC-003. Unknown: n/a.
- **CDC-023** — a register capturing a net that may be driven by an unmodeled black box. Exempt: none. Severity: WARNING (Unresolved). Unknown: scope blackboxes INCOMPLETE.
- **CDC-024** — every recognised protocol (Gray FIFO pair, handshake, toggle) emits an INFO obligation: functional correctness is not verified. Exempt: none. Severity: INFO, proof state NOT_ATTEMPTED. Unknown: never reported as proven.
- **CDC-026** — synchroniser stages on different clock edges or with different asynchronous resets. Exempt: none. Severity: as CDC-003. Unknown: n/a.
- **CDC-027** — a non-final synchroniser stage feeds other logic or a register in another domain. Exempt: an edge detector (stage XOR its delayed copy). Severity: as CDC-003. Unknown: n/a.
- **CDC-028** — capture enabled by a control that is not synchronised from the source domain. Exempt: synchronised qualifiers (CDC-039). Severity: as CDC-003. Unknown: n/a.
- **CDC-029** — a primary input captured by registers: INFO when declared synchronous (set_input_delay -clock C with C Same as the capture clock) or recognised synchroniser; otherwise WARNING. Exempt: none. Severity: WARNING / INFO. Unknown: no input constraint -> Unresolved.
- **CDC-030** — a clock constrained with create_generated_clock: master/generated alignment is assumed by STA but not derived. Exempt: none. Severity: WARNING (Unresolved). Unknown: crossings between master and generated clock stay UNKNOWN.
- **CDC-031** — a clock mux whose select is not synchronous to its input clocks, or whose output clock relation is unknown. Exempt: none. Severity: WARNING (Unresolved). Unknown: n/a.
- **CDC-032** — bits of one source bus captured with different synchroniser depths, or partly synchronised. Exempt: none. Severity: as CDC-003. Unknown: n/a.
- **CDC-033** — recognised Gray pointer: INFO obligation (single-increment property assumed); logic between the Gray register and the crossing is a violation. Exempt: none. Severity: INFO / as CDC-003. Unknown: obligation never discharged.
- **CDC-034** — recognised Gray pointer pair (opposite directions): pointer without asynchronous reset, or reset by different resets. Exempt: none. Severity: WARNING / INFO when reset by one source. Unknown: heuristic.
- **CDC-035** — unpacked-array memory (or whole-symbol crossing of one) written and read in different domains. Exempt: exclusive clocks (INFO). Severity: WARNING (Unresolved), proof NOT_ATTEMPTED. Unknown: vendor RAM semantics not modelled.
- **CDC-036** — a register whose cone combines >= 2 asynchronous influences of which at least one is unsynchronised. Exempt: payload captured under a synchronised qualifier. Severity: WARNING (heuristic). Unknown: n/a.
- **CDC-037** — clocks declared exclusive although a synchroniser was recognised between them. Exempt: none. Severity: WARNING. Unknown: n/a.
- **CDC-038** — a crossing covered by set_false_path / set_multicycle_path. Exempt: set_max_delay / set_min_delay (the path stays timed). Severity: WARNING (the exception never makes a crossing safe). Unknown: n/a.
- **CDC-039** — payload captured under a control synchronised from the same source domain (qualifier pattern): INFO + obligation (data held stable). Exempt: none. Severity: INFO, proof NOT_ATTEMPTED. Unknown: n/a.
- **CDC-040** — a module annotated (* sverity_sync *) / (* cdc_sync *) is checked against the recognised structure inside it (WARNING if none, INFO otherwise); a module named in a --library JSON file (name, kind=synchronizer, clock, input, output, stages) is treated as that many stages on its clock port instead of walking its body (INFO + trust model). Exempt: none. Severity: WARNING / INFO. Unknown: a malformed library entry is CST-018 ERROR and the cell stays an ordinary module; the declaration is trusted, not verified.
- **CDC-041** — synchronised crossing (single- or multi-bit) whose source and synchroniser use different asynchronous resets (WARNING) or where only the source is reset (INFO). Exempt: none. Severity: WARNING / INFO. Unknown: heuristic.
- **CDC-042** — synchronised single-bit crossing of a computed (event-like) signal: no pulse-width guarantee. Exempt: slow-to-fast with known periods is INFO. Severity: WARNING when periods unknown, INFO otherwise; proof NOT_ATTEMPTED. Unknown: heuristic pulse/level classification.
- **CDC-043** — registers on opposite edges of one clock connected by a path; dual-edge registers. Exempt: none. Severity: WARNING (heuristic; dual-edge Unresolved). Unknown: n/a.
- **CDC-044** — a synchronised primary input that feeds an edge detector or a shift chain of >= 4 stages (asynchronous event/serial stream): INFO + obligation (oversampling not verified). Exempt: none. Severity: INFO, proof NOT_ATTEMPTED. Unknown: n/a.

## Crosswalk and licences

Reference projects were studied for rule inventories only; no source code from any of them is included. Vendor documentation (AMD UG906, Altera AN 919)
could not be fetched when this crosswalk was written: their entries are UNVERIFIED snippets, used for IDs/titles only (never copied text).

| Source (licence) | Reference rules → SVerity |
|---|---|
| OpenCDC (Apache-2.0) | unsynchronized crossing → CDC-003; multi-bit → CDC-004; reconvergence → CDC-010/011; gated clock → CDC-020; muxed clock → CDC-019; comb between sync → CDC-006; daisy chain → CDC-027; reset-domain crossing → RDC-004/005; path truncation → CST-010; pulse/toggle → CDC-013/014/042; unknown propagation → LNT-040/054 |
| RTL Buddy CDC (BSD-3-Clause) | 001 → CDC-003; 002/022 → CDC-005; 003/006 → CDC-008; 004/019/020 → CDC-004/009/032; 005 → CDC-010; 008 clock as data → LNT-027; 009/013 → CDC-013/014; 010 clock glitch → CDC-020; 011 input → CDC-029; 012 → CDC-028/039; 014 → CDC-006; 015 → CDC-026/041; 016 → CDC-043; 017 latch → CDC-001; 018 → CDC-027; 021 → CST-011/CDC-002; 023 → CDC-019 |
| AMD Vivado UG906 CDC-1..6, 8, 9, 11 (vendor, UNVERIFIED) | unsynchronised single/multi-bit → CDC-003/004; ASYNC_REG → CDC-012; async clear/preset → CDC-021; reset fan-out → RDC-016 |
| Altera AN 919 CDC-50001..3 (vendor, UNVERIFIED) | → CDC-003, CST-004 (missing constraints), CDC-028 |

Retired IDs: CDC-022 → CST-004 + CDC-038, CDC-025 → CDC-008. OpenCDC and RTL Buddy code may be reused under their licences if notices are kept; none is.
