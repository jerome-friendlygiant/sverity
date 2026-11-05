# SVerity roadmap

Where SVerity stands, what comes next and why. This page owns open work and the research behind it. The [RFC](SVerity_RFC.md) owns the specification and the [1.0 criteria](SVerity_RFC.md#8-acceptance-criteria-for-10); the [catalog](rules/catalog.json) owns the status and gap of each rule.

**SVerity has not been compared against any commercial tool on any design. Nothing here claims equivalence, coverage parity or signoff qualification.**

Horizons say what we mean to do next, not how long it takes. Nothing here is dropped by being far away: research for every horizon is in [section 4](#4-capability-research) and [its sources](#sources-and-provenance).

## 1. Short term: fixes and finishing what is started

These are bugs and unfinished work, not limits of the approach. Each is fixed when its fixture passes and the item is removed from this list.

- Case analysis over-collects: a read such as `$display(..., m)` or an index `v[m]` in a procedure rejects a value on `m`, and stops propagation through `wire x = m`. This keeps findings, so it is safe, but it costs a usable constraint. Separate reads from writes.
- RDC-024 can count one reset use twice when it reads two stages of one synchroniser, as in `s2 & s3`.
- CDC-041 compares the source's reset with the last register of the walk, which may be a follower past the required depth. RDC-005 still reports such a mismatch.
- `syncedOut_` also holds the last stage of a chain shorter than the required depth, so that stage can still qualify a payload. CDC-005 reports the short chain as well, so this is not silent, but the meaning should be made exact.

## 2. Mid term: reaching 1.0

The target is the [1.0 criteria](SVerity_RFC.md#8-acceptance-criteria-for-10). It is deliberately a large one: 1.0 validates the rules we keep, not a small subset.

**What 1.0 claims.** Validated SystemVerilog RTL lint and structural CDC and RDC analysis, for named workflows, configurations and design sizes, with every functional and physical obligation stated. Validating rules against our own specification does not make SVerity a substitute for a commercial tool, because a team could still be unable to switch. So 1.0 is bounded, and replacement is earned family by family in the phases below. **A family claims to replace a commercial tool only after passing its own gate.** Until then the release says it does not.

| Phase | Work | Gate to pass |
|---|---|---|
| **1. Measure** | An accuracy suite of independently labelled good, bad and unknown designs that were not used to develop the rules. Budgets for false alarms, missed defects, unresolved findings and review effort per family, fixed before evaluation. Capacity and review budgets on stated hardware. | The suite and budgets are published and a baseline is measured. No seeded critical defect is silently accepted. |
| **2. Lint replacement** | Name the workflow and the defect classes it needs, and build the lint deck from those rather than from what exists. Migrate a real team's lint policy and waivers. | The deck is validated and meets its budgets, and an independent pilot switches, with every exclusion listed. |
| **3. CDC replacement** | Complete crossing inventory under reviewed clock intent, then a way to close what structure cannot show: stability of a qualified payload, protocols, reconvergence and glitches. Modes and exceptions, boundary contracts at block level, and library cells. | As for lint. Every obligation has a named closure step, inside the tool or accepted outside it. |
| **4. RDC replacement** | Reset trees, independently asserted resets, user-declared sequencing, release order, and a model of a stopped clock and a held or released reset. The furthest behind, so it comes last. | As for lint. Sequencing can be stated and checked, not only reported as unresolved. |
| **5. Integration** | ASIC and FPGA profile bundles, reconciling existing constraints and waivers, baselines and CI, and a full-SoC run with an SDC within budget. | Two independent pilot workflows run end to end, with anything unsupported visible and blocking a safety claim. |

Phases 2 to 4 can overlap, and a phase can draw from the [long-term goals](#3-long-term-beyond-10) what its named workflow proves it needs. Passing every gate is what would make SVerity a good replacement. If only some pass when 1.0 ships, its claim names those families and no others.

**Not in the 1.0 claim.** VHDL and mixed language, gate-level analysis, power-aware analysis, formal proof and reusable block summaries, unless a named workflow pulls one in. A team that relies on one of these keeps its commercial tool for that job, and the release says so.

**Showing it without a commercial licence.** Commercial output is a useful reference but not ground truth, and a licence may forbid publishing it. The evidence is independently labelled designs, held-out cases, reproducible resource measurements, documented closure effort and private adopter trials, with aggregate results published where agreements allow.

**Decisions still needed.** Who the first adopters are and which workflows they need. The numeric budgets, including acceptable false-alarm noise and residual risk. The capacity envelope: 10 minutes and 16 GiB for `top_earlgrey` is a working proposal, not a commercial benchmark. The pilots, which need real teams and cannot be reached by engineering alone.

**Validate in widening decks.** Each deck stays open until every rule in it has a written severity policy, good, bad, boundary, unknown and mutation fixtures, and a passed independent adversarial review. Independent review may be a separate AI reviewer, and the report says so; it is not a human signoff.

| Deck | Contents | What it allows |
|---|---|---|
| Core | 2-flop and Gray-FIFO CDC, reset synchronisers, main structural lint | The first `validated` rules; shows the process works |
| Domain | All structural and protocol CDC and all RDC, with the CST rules they depend on | A meaningful CDC and RDC claim |
| 1.0 | All of the above, the remaining lint and CST rules | Freezing 1.0. A rule leaves the deck only for a written reason, decided before the freeze |

Each rule's `deck` field in the [catalog](rules/catalog.json) names the earliest deck holding it, and `tools/dev/conformance.py` shows how far each rule and deck is from `validated`. No rule is `validated` yet.

**Coverage per deck.** `--deck` reports coverage for a deck, and no run can yet reach `COMPLETE`, because the decks hold rules that are still partial. A run can report `COMPLETE` for a frozen deck when all its rules ran and every limit it hit was disclosed. Unfinished rules outside the deck stay available as preview. This is what finally allows an honest clean exit, so the deck has to be validated first.

**Capacity: OpenTitan Earl Grey.** 1.0 analyses the whole `top_earlgrey` SoC at a pinned commit, with published time, memory and coverage on stated hardware. Earlier runs measured about 19 MB per PicoRV32 copy, growing linearly, up to 170k register bits in 37 s and 1.3 GB. Steps: pin it in the public project manifest with a reproducible file list, profile bytes per edge and time per rule, and decide the policy for symbols over 16384 bits, which collapse to one node today.

`tools/dev/capacity.py` reproduces these from a clean checkout: OpenTitan commit `92f3d03`, a file list from FuseSoC 2.4.5, `--single-unit`, no SDC, on a shared machine. It caps memory so that running out ends in a reported failure instead of disturbing other processes. `--timings` gives time and peak memory per phase and rule.

| Top, date | Result | Time | Peak memory | Register bits | Graph |
|---|---|---:|---:|---:|---|
| `uart`, 2026-11-04 | Finished, INCOMPLETE | 1.6 s | 0.4 GB | 1,093 | 6,896 nodes, 49,277 edges |
| `uart`, 2026-11-05 | Finished, INCOMPLETE, no ERROR | 1.6 s | 0.3 GB | 1,093 | 6,930 nodes, 14,579 edges |
| `aes`, 2026-11-04 | Finished, INCOMPLETE, 1 false ERROR | 118 s | 0.9 GB | 7,478 | 118,344 nodes, 4,589,948 edges |
| `aes`, 2026-11-05 | Finished, INCOMPLETE, no ERROR | 4.6 s | 0.4 GB | 7,478 | 118,947 nodes, 454,185 edges |
| `top_earlgrey`, 2026-11-04 | Killed by the host's memory guard | 42 s | 9.1 GB at the kill | Unknown | Unknown |
| `top_earlgrey`, 2026-11-05 | Lint finished with no ERROR. CDC and RDC ran out of memory at the 13.5 GB limit, so INCOMPLETE | 112 s | 10.7 GB | 237,159 | 3,266,136 nodes, 15,507,552 edges |

The target is not met: **`top_earlgrey` has not yet finished with CDC enabled.** The model builds in 9 s and every lint rule completes, including LNT-012, where 368 false ERRORs are now 0 (363 Unresolved and 54 heuristic WARNINGs remain to read). CDC and RDC each fail with `std::bad_alloc` in the fan-in walk at the 13.5 GB limit. The [cone budgets](rules/ANALYSIS.md#how-cones-are-computed-once) come to about 2.5 GB, yet the CDC pass takes the run from 3.6 GB to 10.7 GB, so most of that memory is something the budgets do not count.

Next, in order:

1. Find what holds the memory in the CDC pass, with counters. Candidates, none measured: spare capacity in stored cones, each crossing group's per-source sets, and the per-input lists in `analyseCrossings`.
2. Finish one `top_earlgrey` run and read its findings against the RTL. Then look at time again: register cones past their budget are recomputed, and name-building shows up in the RDC pass.
3. Run with an SDC, because 76 clock domains with unknown relationships are the worst case for crossing analysis.
4. Add a check that a binary reproduces its own report, since [determinism](rules/ANALYSIS.md#determinism) is otherwise only checked by hand.

**Beyond the public gate.** Running OpenTitan's IPs found a false alarm that all six gate designs had passed, so widen what is run regularly. Still open from that sweep: `usbdev` and `spi_device` over a 150 s limit, measured before the cache fix; and `flash_ctrl`, `pwm` and `pattgen`, which did not run cleanly as tops.

**Shared analysis.** Fix these in the analysis, not rule by rule ([AGENTS.md](../AGENTS.md)). Each stays reported as Unresolved until then.

- Model a stopped clock and a held or released reset, so `set_case_analysis` can apply to clock and reset sources. A stopped clock does not make its registers constant, and a released reset does not say what they start from: this needs retained state, per-register reset state and a stated obligation for entering the mode. Until then these stay unapplied (CST-009).
- Give case values an instance, so a module with several instances can take one on a single instance if its symbols are shared.
- `Flow` walks loops once and treats mid-body timing controls as opaque.
- Guards are not kept as AND/OR trees.
- X and Z values are not propagated through variables.
- Interfaces are not followed through the flat model.
- LNT-044 and LNT-066 are approximate; slang's case-coverage and `continue` handling are too (LNT-013 reports Unresolved).

**Foundations that follow, in priority order.** This is judgement, not a vendor requirement.

1. **Constraint trust and coverage.** Wrong intent can hide a real crossing ([S6]). Make supported declarations, conflicts, generated-clock assumptions and unapplied modes unambiguous before increasing recognition ([prerequisites](rules/ANALYSIS.md#prerequisites-of-every-run)).
2. **Complete extraction with honest unknowns.** Follow model fallbacks and opaque constructs through to affected checks and reports. An unexamined cone must not look clean ([analysis contract](rules/ANALYSIS.md), `--explain CST-010`).
3. **IO and IP boundary contracts.** A correct internal chain says nothing about external launch, capture or reset assumptions. Do input and macro contracts before reusable summaries (`--explain CST-012`, `CST-017`).
4. **Adversarial CDC and RDC recognition.** Check glitch and reconvergence lookalikes, payload capture, reset sequencing and cross-domain tolerance. Require a trustworthy structural conclusion or an explicit obligation, never formal proof as a hidden dependency.
5. **Independent release and report validation.** Reconcile inventories and retain evidence for every unknown, waiver and configuration ([diagnostic contract](SVerity_RFC.md#3-diagnostic-contract-non-negotiable)).

**Also needed for 1.0**

- ASIC and FPGA profile bundles, including cell metadata for CDC-012. They are undefined today.
- Written severity policies for the rules that lack one, and each rule's `default_severity`. `tools/dev/conformance.py` lists them.
- Implement or drop the CST rules with no emitter: CST-003, 007, 008, 013, 015, 016, 017, 020, 021, 023 and 024. They sit in the decks and keep those decks from COMPLETE.
- A differential run against a commercial tool, if legitimate access exists. Without it the release says that no comparison was made.

## 3. Long term: beyond 1.0

These are real goals, ordered by how much they build on 1.0. None may displace the foundations above, and each keeps the core rule: unknown is never safe. Research is in [section 4](#4-capability-research).

| Goal | Direction |
|---|---|
| **Formal-assisted CDC, RDC and lint** | Optional transition-system backend for handshake and FIFO protocols, Gray-code transitions, FSM reachability and reset sequencing. Proof status per the [charter](SVerity_RFC.md#1-product-charter); a solver never becomes a dependency of the structural core. |
| **Power-aware analysis** | A UPF subset first, then a power-intent model, power-state semantics, isolation and retention cells and crossing checks. Missing intent stays disclosed. |
| **Billion-gate scale** | Hierarchical flow and versioned block summaries with invalidation and proof and assumption propagation, then caching. Precision loss stays visible. |
| **GUI and debug** | Schematic and waveform tracing as a consumer of stable evidence and path APIs, not a part of the analyzer. |
| **Constraint generation** | Reviewable candidates with provenance and uncertainty. Inferred intent is never silently promoted to a trusted relationship. |
| **Multimode sweep** | Reproducible configuration manifests, automatic mode sweeps and cross-mode validation. |
| **SGDC import** | Only from legitimately obtained documentation. |
| **More rule families** | SVA lint, source style and certification policy decks, each from public specifications with provenance and independent validation. |
| **Distribution** | Hosted CI, container and prebuilt binaries. |
| **Community** | A second maintainer and independent approval of rule changes. "Industry standard" is an adoption goal the project cannot confer on itself. |

## 4. Capability research

Public-source research and CLI checks were performed on 2026-11-03. Commercial statements describe only what the linked publisher documents; versions, availability and behaviour may have changed. Undocumented behaviour is **unknown**. Vendor performance and signoff language is a vendor claim, not an independently measured result. Capabilities from different products are not necessarily available together in one edition.

SVerity's column was checked against the reviewed `sverity 0.1.0` binary (slang 12.1.0, catalog `0.1-draft`). The [catalog](rules/catalog.json) stays authoritative for individual rules.

There are two different completion targets: delivering SVerity's advertised 1.0 coverage, and matching the wider commercial capability envelope. The [phases to 1.0](#2-mid-term-reaching-10) make replacement a tested claim per family for named workflows; matching the whole envelope is what the long-term goals move towards. The following checklist is a research-based capability assessment, not a replacement for the [catalog](rules/catalog.json). **Present** means exercised or exposed by the checked CLI; **partial** means some capability exists; **absent** means no functioning facility was established; **deferred** means explicitly future work in the RFC; **out of scope** means deliberately excluded by RFC section 1. A marketing page alone does not establish rule depth, soundness or performance.

| Capability to assess | Public capability evidence | SVerity today and verification | What completion would require |
|---|---|---|---|
| Structural / synthesizability lint depth | VC SpyGlass Lint advertises language, structural and formal-assisted analysis ([S1], [S13]). | **Partial:** native lint predicates are exposed by `--list-rules`; `--explain LNT-004` and `LNT-013` establish width/latch checks. `LNT-071` is an obligation, not reachability proof. [Lint analysis limits](rules/ANALYSIS.md#lint). | Validate every advertised predicate against its title, severity policy and independent unsafe/unknown cases; measure semantic coverage, not number of rule names. |
| Structural CDC and protocol recognition | Synopsys describes structural/functional analysis and protocol recognition ([S2]). | **Partial:** chains, bus/handshake/FIFO structure and explicit protocol obligations (`--explain CDC-003`, `CDC-024`, `CDC-039`; [CDC analysis](rules/ANALYSIS.md#cdc)). | Close extraction/recognition blind spots conservatively; verify unsafe lookalikes and preserve unproved protocol conditions. |
| Functional/formal CDC; assumption validation | VC SpyGlass documents inbuilt formal and generated SVA for protocols/assumptions ([S2]); Real Intent describes assertion verification of constraints ([S6]). | **Deferred:** `CDC-024` / `CDC-039` report unattempted proof. The [RFC future-proof contract](SVerity_RFC.md#1-product-charter) allows an optional extension, not a mandatory runtime solver. | Optional transition-system backend, explicit assumptions, bounded/unbounded scope, witnesses and UNKNOWN handling; not a prerequisite for claiming only structural 1.0 coverage. |
| Glitch potential, reconvergence and coherence | AMD publishes combinational/multi-clock checks ([S9]); Questa RDC documents reconvergence ([S7]). | **Partial:** `--explain CDC-008`, `CDC-010`, `CDC-032`, `RDC-009` establish structural/heuristic checks, not glitch-freedom proofs. | Broaden cone and correlation analysis with explicit uncertainty; functional hazard proofs belong to the optional proof extension. |
| Reset-domain compatibility and sequencing | VC SpyGlass RDC describes reset relationships and qualifiers ([S14]); Meridian RDC documents reset scenarios and blockers ([S8]). | **Partial:** `--explain RDC-003`, `RDC-004`, `RDC-005`, `RDC-011`, `RDC-023` distinguish structures and obligations. [RDC analysis](rules/ANALYSIS.md#rdc). | Specify reset assumptions and scenario/boundary contracts, check assertion/release interactions and masking without claiming sequence proof from topology. |
| Constraint semantics and completeness | SDC support is public ([S3]); Real Intent illustrates the danger of incorrect clock constraints ([S6]). | **Partial:** `--help`, `--explain CST-009`, `CST-012`, `CST-014`, `CST-018`; output delays and scalar top-level case analysis are applied with disclosure, other case targets stay unapplied, and coverage remains INCOMPLETE ([prerequisites](rules/ANALYSIS.md#prerequisites-of-every-run)). | Apply each advertised declaration consistently or reject/disclose it; validate references, conflicts, generated clocks and unsupported evaluation against the design. Full Tcl/SGDC compatibility is not implied. |
| Automatic setup / constraint generation | Synopsys documents clock/reset extraction ([S3]) and generated additional constraints in its ML-RCA white paper ([S5]). | **Partial** for inventories, **absent** for an exported candidate-constraint generator: `--list-rules` exposes domain inventories; `--help` has no such export. | Generate reviewable candidates with provenance and uncertainty; never silently promote inferred intent to a trusted relationship. Lower priority than validating supplied intent. |
| IO / IP boundaries and reusable hierarchical abstraction | Synopsys offers signoff abstract models ([S2]). | **Partial** for trusted cell metadata, **absent** for verified block summaries: `--library`, `--explain CDC-029`, `CDC-040`, `CST-012`, `CST-017`; [architecture](ARCHITECTURE.md). | Input/output clock/reset/protocol contracts first; later versioned summaries with invalidation and proof/assumption propagation. A black box must remain visible as uncertainty. |
| Multimode / configuration coverage | Meridian RDC describes mode-specific runs ([S8]). | **Partial:** parameterized elaboration is available; automatic sweep/coverage accounting is not (`--help`, `--explain CST-014`, `CST-015`; [prerequisites](rules/ANALYSIS.md#prerequisites-of-every-run)). | Reproducible configuration manifests, consistent mode application and disclosed unexamined alternatives. Independent explicit runs remain a practical interim workflow. |
| Power-domain crossings, isolation and retention | UPF-aware CDC is advertised by Synopsys ([S3]); Questa RDC covers isolation/retention ([S7]). | **Absent:** `--explain RDC-012`, `RDC-029` and `--help` establish no UPF/CPF input. Not an explicit RFC section 1 exclusion. | Power-intent parser/model, power-state semantics, cell contracts and crossing checks; agree release scope before advertising power-aware safety. Missing intent must remain disclosed. |
| Reports, review and waiver integrity | Questa documents reporting/waiver management ([S7]); Synopsys documents cluster-based triage ([S5]). | **Present** for JSON/SARIF/HTML exports and visible waiver disposition (checked runs); **partial** for end-to-end completeness assurance (`--explain CST-020`; [report contract](SVerity_RFC.md#3-diagnostic-contract-non-negotiable)). | Independently reconcile crossing inventories, evidence and report outputs; test that unsupported scopes and waived occurrences cannot disappear. |
| Interactive debug GUI / schematic and waveform tracing | Synopsys advertises Verdi cross-probing ([S1]); Questa RDC documents schematic/waveform displays ([S7]). | **Absent** as a native interactive design debugger; `--html` is an exported report (`--help`, checked export). The charter promises a CLI/library, not a GUI. | Optional consumer of stable evidence/path APIs; useful for adoption, not required for structural correctness or 1.0 CLI completion. |
| Flow scale / SoC capacity | Hierarchical scalability is advertised ([S2]); Questa RDC claims billion-gate capacity ([S7]). | **Partial:** flat model with recorded precision limits ([analysis prerequisites](rules/ANALYSIS.md#prerequisites-of-every-run)); the [measured runs](#2-mid-term-reaching-10) have not finished `top_earlgrey`. Comparable capacity is **unknown**. | Publish reproducible public-design resource/coverage benchmarks; address bottlenecks without hiding precision loss, then evaluate summaries/caching. No commercial speed ratio is established. |
| Formal-assisted functional lint | VC SpyGlass Lint advertises formal depth ([S13]). | **Deferred:** `--explain LNT-071`, [future proof goal](SVerity_RFC.md#1-product-charter). | Optional proof backend and conformance evidence; keep structural lint claims bounded until then. |
| Style policies / certification rule decks | Synopsys advertises STARC/OpenMORE and safety-related decks ([S1]); OpenTitan distinguishes style lint ([S12]). | Style is **outside 1.0 scope** ([RFC scope and families](SVerity_RFC.md#4-rules)); no certified policy equivalence is established. | Not a 1.0 gap. Any later policy needs public specifications, provenance and independent validation; a checklist is not certification. |
| Simulation, STA signoff and physical guarantees | Synopsys offers simulation-based CDC support ([S2]); Altera recommends timing-based metastability reporting ([S10]). | **Out of scope:** simulation and STA signoff are explicit [charter exclusions](SVerity_RFC.md#1-product-charter). `--explain RDC-013` preserves physical obligations. | Not a gap to close inside this tool. Supply external timing/MTBF evidence where a project's acceptance process requires it. |

### What review has and has not shown

An independent review (2026-11) built the tool and ran it on hand-written cases and public RTL. Crossings through interfaces, functions, hierarchical references, structs, generate loops, memories, latches and clock muxes were all reported; reconvergence, first-stage fan-out, mixed edges, third-clock stages and pulse loss were caught. This is evidence from a few dozen cases, not proof that no false-safe results remain. Cases from earlier audits stay in the rule fixture directories under descriptive names.

Three more independent AI reviews (2026-11-05, a separate model running the binary on about 200 hand-written cases, with the earlier binary for comparison) hunted false-safe results in the changes behind this page. They found real defects each round, which are fixed and kept as fixtures: an override or waiver of one rule accepting another fact of the same crossing, a stage enabled by its own data recognised as a synchroniser, a precise loop lowered by a neighbouring approximate branch. A fourth round has not been run on the last fixes, and these are AI reviews, not human sign-off. The hard parts of the analysis stay unproved by this: the reviews list what they did not reach.

## 5. Coming from a commercial flow

| Flow concept and public evidence | SVerity mapping and migration action |
|---|---|
| **Goals / methodology.** Synopsys advertises GuideWare methodology and multiple lint policies ([S1]). Its CDC datasheet describes setup, setup checks, integration, chip analysis, review and signoff stages ([S2]). Public evidence here does not establish the exact command semantics of a SpyGlass goal. | `--goal lint`, `--goal cdc`, `--goal rdc` select families; repeating them selects a union. They are not imports of vendor methodologies. `--config`, `--enable-rule`, `--disable-rule`, `--option` and `--severity` configure the run. Disabled checks still affect coverage. Verified in `--help` and the [tutorial run](TUTORIAL.md); inspect `--list-rules` rather than copying a commercial goal name. |
| **SDC / SGDC constraints.** Synopsys documents SDC-based CDC ([S3]); its Fault Analysis datasheet explicitly names SDC and SpyGlass SGDC as constraint formats ([S4]). That latter source is evidence of the format name, not a specification of CDC SGDC commands or translation. | Supply supported SDC via repeatable `--sdc`. There is no SGDC importer in `--help`; translate intent manually and review every rejected or unapplied declaration. This is an SDC subset, not a Tcl interpreter or timing engine. Output delays feed the CST-012 boundary check. Case analysis applies only on scalar top-level data inputs, is disclosed by CST-014 and leaves coverage INCOMPLETE; other targets are reported as unapplied. See [constraint prerequisites](rules/ANALYSIS.md#prerequisites-of-every-run) and the [parser](../src/constraints/sdc.cpp). Do not mechanically rename an SGDC file to SDC. |
| **Assumptions and waivers.** Synopsys documents a CDC violation/waiver lifecycle ([S5]); Real Intent's vendor-hosted case study distinguishes constraints expressing assumptions from waivers and warns that incorrect clock relationships can hide missing synchronizers ([S6]). | Reassess each waiver against a SVerity predicate. `--waivers`, source comments and `--waiver-template` exist; the template deliberately needs human owner/reason values. There is no commercial waiver import switch. Waived findings retain severity and evidence, and do not prove safety or improve coverage. The checked [tutorial waiver](tutorial/waivers.json) kept the ERROR visible as WAIVED. Syntax and narrowing requirements: [diagnostic contract](SVerity_RFC.md#3-diagnostic-contract-non-negotiable), [tutorial](TUTORIAL.md). |
| **Reports and triage.** Questa RDC documents text/GUI reporting, custom reporting and waveform/schematic debug ([S7]); Synopsys documents clustered CDC review ([S5]). | `--json` is canonical; `--sarif` and `--html` export reports. `--group` and `--max-console` require a complete JSON or SARIF export. They are console views, not a schematic debugger or formal counterexample viewer. All three exports were generated successfully. Review execution, coverage and review status separately, using the [report contract](SVerity_RFC.md#3-diagnostic-contract-non-negotiable). |
| **Abstraction and integration.** Synopsys documents hierarchical CDC with signoff abstract models ([S2]). | SVerity elaborates design hierarchy, then analyzes a flat model; that is not block-summary signoff. `--library` supplies trusted cell descriptions, not proven reusable block contracts (`--explain CDC-040`, [architecture](ARCHITECTURE.md)). Missing source can leave unresolved black boxes. Analyze complete source where possible and review all boundary uncertainty. |
| **Configuration coverage.** Meridian RDC's public case study describes separate reset/mode scenarios and external pin assumptions ([S8]). | Each SVerity run covers one elaborated parameter configuration; use slang filelists, defines, include paths, `--top` and parameter overrides as shown by `--help`. Run each relevant configuration separately. There is no automatic mode/configuration sweep in the CLI. Do not interpret a run on one generate branch as evidence about the others ([analysis prerequisites](rules/ANALYSIS.md#prerequisites-of-every-run)). |

A reproducible starting point, using the repository's own small examples:

```sh
sverity --help
sverity --list-rules
sverity --explain CDC-003
sverity --goal cdc --top top --sdc docs/tutorial/clocks.sdc \
  --json report.json --sarif report.sarif --html report.html docs/tutorial/bad.sv
sverity --goal cdc --top top --sdc docs/tutorial/clocks.sdc \
  --waivers docs/tutorial/waivers.json --json waived.json docs/tutorial/bad.sv
```

The checked first design run returned exit **5**: execution SUCCESS, coverage INCOMPLETE, review FAIL, **1 ERROR, 2 WARNING, 4 INFO**. The waiver run returned **6**, preserved the same severity counts, marked **one finding WAIVED**, and required review. These are observations on those inputs, not expected counts for another design or version. Strict exit bits are documented by `--help` and the [tutorial](TUTORIAL.md); a zero exit under `--exit-policy violations` does not establish complete coverage.

For a real migration, first reproduce the intended source/configuration set, then review constraints and modeled boundaries, then evaluate each finding and waiver. Keep the commercial flow as an independent reference until your own acceptance process establishes otherwise. No commercial executable or output was used to validate this guide.

### Publicly sourced rule names

**Closest is never equivalent.** The table contains nine named checks from official FPGA vendor documentation, not invented SpyGlass tags. Each SVerity candidate was read with `--explain`. “Partial” means overlapping intent with different recognition, domain assumptions, exemptions or coverage; it is not the catalog status. “None” means no corresponding predicate was established. No “same predicate” claim is warranted by this evidence.

| Public check name | Public meaning (paraphrased) | Closest SVerity rule | How close and why |
|---|---|---|---|
| AMD `CDC-1` | Single-bit crossing lacks known synchronization ([S9]). | `CDC-003` | **Partial:** flop-to-flop predicate with its own qualifier exemptions and clock-relationship model. |
| AMD `CDC-4` | Bus crossing lacks known synchronization ([S9]). | `CDC-004` | **Partial:** grouped crossing analysis; qualified payloads and individually synchronized bits take separate paths. |
| AMD `CDC-2` | Synchronizer lacks `ASYNC_REG` on some/all stages ([S9]). | `CDC-012` | **Partial:** profile-dependent attribute policy also accepts SVerity annotations. |
| AMD `CDC-7` | Async clear/preset lacks known synchronization ([S9]). | `RDC-003` | **Partial:** reset-release recognition and routing of other-domain sources differ; no timing proof. |
| AMD `CDC-10` | Logic feeds a synchronization circuit ([S9]). | `CDC-008` | **Partial:** capture-cone operator/guard test. Logic *between* stages is separately `CDC-006`; similar names must not be mapped by number. |
| AMD `CDC-12` | Synchronizer fanin contains multiple source clocks ([S9]). | `CDC-010` | **Partial:** heuristic asynchronous-influence/reconvergence analysis has a broader destination-cone predicate. |
| AMD `CDC-26` | Possible LUTRAM read/write collision ([S9]). | None; a [written exclusion](rules/CDC.md#crosswalk-and-licences) (`CDC-035` reports the memory). | **None:** cross-clock memory identification does not establish a LUTRAM collision predicate. |
| Altera `CDC-50001` | Unsynchronized asynchronous single-bit transfer ([S10]). | `CDC-003`, with `CDC-008` for source logic | **Partial:** Altera also defines asynchronous transfers using parent clocks, false paths and forced synchronizer identification; do not assume SVerity interprets those identically. |
| Altera `CDC-50005` | CDC bus bits use unequal synchronization lengths ([S11]). | `CDC-032` | **Partial:** overlapping unequal-depth predicate; bus grouping, chain recognition and supported technology are not established as identical. |

Searches included `site.synopsys.com VC SpyGlass CDC constraints SGDC hierarchical formal datasheet`, `site.synopsys.com SpyGlass "SGDC" "goals"`, `site.synopsys.com SpyGlass "waivers" methodology`, OpenTitan SpyGlass/lint/CDC goal searches, `site.docs.amd.com "CDC-1" "CDC-10"`, and Altera Design Assistant `CDC-50001` searches. Meridian and Questa capability searches informed section 4. Exact SpyGlass lint `W*` predicates and `cdc_setup` / `cdc_verify` goal definitions were **not established in an allowed source** during this search. Unofficial manuals surfaced but were excluded. OpenTitan's public lint methodology says its proprietary lint policy is not published and its CDC collateral is handled offline ([S12]); it cannot supply the missing SpyGlass definitions. Absence from this search is not absence from a product.

### Sources and provenance

Links were consulted as public publisher material; no manuals, vendor-tool output, waiver decks or source code were imported. Vendor documentation is copyrighted publisher material, not assumed open-licensed: descriptions above are short paraphrases. Dates/versions identify the evidence, not the installed commercial release.

| Ref | Public source and provenance |
|---|---|
| [S1] | [Synopsys VC SpyGlass Lint product page](https://www.synopsys.com/verification/static-and-formal-verification/vc-spyglass/vc-spyglass-lint.html), vendor-published, copyright Synopsys; live page. |
| [S2] | [Synopsys VC SpyGlass CDC datasheet](https://www.synopsys.com/content/dam/synopsys/verification/datasheets/vc-spyglass-cdc-ds.pdf), vendor-published, ©2020 Synopsys. |
| [S3] | [Synopsys VC SpyGlass CDC product page](https://www.synopsys.com/verification/static-and-formal-verification/vc-spyglass/vc-spyglass-cdc.html), vendor-published, copyright Synopsys; live page. |
| [S4] | [Synopsys VC SpyGlass Fault Analysis datasheet](https://www.synopsys.com/content/dam/synopsys/verification/datasheets/vc-spyglass-fault-analysis-ds.pdf), vendor-published, copyright Synopsys; format-name evidence only. |
| [S5] | [Synopsys VC SpyGlass ML-RCA white paper](https://www.synopsys.com/content/dam/synopsys/verification/white-papers/vc-spyglass-ml-rca-wp.pdf), vendor-published, ©2022 Synopsys, November 2022. |
| [S6] | [Real Intent / Western Digital constraint-based CDC methodology](https://www.realintent.com/clock-domain-crossing-constraints-based-sign-off-methodology-western-digital/), vendor-hosted case study describing DAC 2021 work; copyright Real Intent. Used as vendor material, not as conference-hosted proceedings or independent evaluation. |
| [S7] | [Siemens Questa RDC fact sheet](https://static.sw.cdn.siemens.com/siemens-disw-assets/public/78491/en-US/Siemens-SW-Questa-RDC-Reset-Domain-Crossing-Verification-FS-78491-C3.pdf), vendor-published, copyright Siemens. Version-specific UPF/support claims must be rechecked for a current release. |
| [S8] | [Real Intent / Rivos RDC methodology](https://www.realintent.com/reset-domain-crossing-methodology-ai-llm-chips-rivos/), vendor-hosted case study, copyright Real Intent; capability/workflow evidence, not independent validation. |
| [S9] | [AMD UG906, CDC report rules, 2021.2](https://docs.amd.com/r/2021.2-English/ug906-vivado-design-analysis/Understanding-the-Clock-Domain-Crossings-Report-Rules), official vendor methodology documentation, copyright AMD/Xilinx. |
| [S10] | [Altera-hosted Quartus 21.3 help, CDC-50001](https://resources.altera.com/quartushelp/21.3/da_rules/cdc_50001.htm), official vendor rule documentation, Intel-era release; no open licence assumed. |
| [S11] | [Altera-hosted Quartus 21.3 help, CDC-50005](https://resources.altera.com/quartushelp/21.3/da_rules/cdc_50005.htm), official vendor rule documentation, Intel-era release; no open licence assumed. |
| [S12] | [OpenTitan public lint methodology](https://opentitan.org/book/hw/lint/), first-party project documentation; page does not supply a licence grant for proprietary policy material, which is explicitly unavailable. No waiver files or generated reports were used. |
| [S13] | [Synopsys VC SpyGlass Lint datasheet](https://www.synopsys.com/content/dam/synopsys/verification/datasheets/vc-spyglass-lint-ds.pdf), vendor-published, copyright Synopsys. |
| [S14] | [Synopsys VC SpyGlass RDC datasheet](https://www.synopsys.com/content/dam/synopsys/verification/datasheets/vc-spyglass-rdc-ds.pdf), vendor-published, ©2020 Synopsys. |

Skipped search results included purported vendor manuals and rule-reference excerpts on third-party sites: publication rights were not established and they fall outside the allowed sources. Also excluded generated OpenTitan CDC report results and an OpTiMSoC commercial waiver file; neither supplied an acceptable basis for copying proprietary tool findings or rule definitions. No unsupported rule name from those results was admitted to the table.

[S1]: https://www.synopsys.com/verification/static-and-formal-verification/vc-spyglass/vc-spyglass-lint.html
[S2]: https://www.synopsys.com/content/dam/synopsys/verification/datasheets/vc-spyglass-cdc-ds.pdf
[S3]: https://www.synopsys.com/verification/static-and-formal-verification/vc-spyglass/vc-spyglass-cdc.html
[S4]: https://www.synopsys.com/content/dam/synopsys/verification/datasheets/vc-spyglass-fault-analysis-ds.pdf
[S5]: https://www.synopsys.com/content/dam/synopsys/verification/white-papers/vc-spyglass-ml-rca-wp.pdf
[S6]: https://www.realintent.com/clock-domain-crossing-constraints-based-sign-off-methodology-western-digital/
[S7]: https://static.sw.cdn.siemens.com/siemens-disw-assets/public/78491/en-US/Siemens-SW-Questa-RDC-Reset-Domain-Crossing-Verification-FS-78491-C3.pdf
[S8]: https://www.realintent.com/reset-domain-crossing-methodology-ai-llm-chips-rivos/
[S9]: https://docs.amd.com/r/2021.2-English/ug906-vivado-design-analysis/Understanding-the-Clock-Domain-Crossings-Report-Rules
[S10]: https://resources.altera.com/quartushelp/21.3/da_rules/cdc_50001.htm
[S11]: https://resources.altera.com/quartushelp/21.3/da_rules/cdc_50005.htm
[S12]: https://opentitan.org/book/hw/lint/
[S13]: https://www.synopsys.com/content/dam/synopsys/verification/datasheets/vc-spyglass-lint-ds.pdf
[S14]: https://www.synopsys.com/content/dam/synopsys/verification/datasheets/vc-spyglass-rdc-ds.pdf
