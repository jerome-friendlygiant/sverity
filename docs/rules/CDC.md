# CDC - Clock-domain crossings and synchronizer/protocol structure

One table row per catalogued rule (titles must match `catalog.json`; a unit test checks this). Add a section
per rule below the table when it is specified: trigger, exemptions, severity policy, evidence, tests.

| ID | Required check |
|---|---|
| `CDC-001` | Clock roots extracted for every sequential element |
| `CDC-002` | Generated/gated clock relationship unresolved |
| `CDC-003` | Asynchronous single-bit flop-to-flop crossing without recognized synchronization |
| `CDC-004` | Asynchronous multi-bit crossing without recognized safe protocol |
| `CDC-005` | Synchronizer depth too short or first stage used unsafely |
| `CDC-006` | Combinational logic between synchronizer stages |
| `CDC-007` | Synchronizer first-stage output fans out into functional logic |
| `CDC-008` | Combinational/glitch-prone source before synchronizer |
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
| `CDC-022` | Unconstrained, cut or unknown crossing with unjustified waiver |
| `CDC-023` | Unrecognized macro/IP crossing boundary |
| `CDC-024` | Verify actual FIFO/handshake functional correctness |
| `CDC-025` | Unregistered or glitch-sensitive source feeding synchronizer |
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
