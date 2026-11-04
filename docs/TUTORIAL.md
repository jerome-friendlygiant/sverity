# First CDC run

Build SVerity as in the [README](../README.md), then run these commands from the repository root. Here `sverity` means `build/sverity` on your PATH. The example is illustrative, not production RTL. The excerpts omit other findings.

```sh
sverity --goal cdc --top top docs/tutorial/bad.sv
```

The first register launches `src` on `a`; the second captures it on `b`. The outcome appears first, followed by every finding in severity order. The run reports:

```text
Result: REVIEW_REQUIRED, exit 6 (REVIEW_REQUIRED + INCOMPLETE; strict policy)
  execution  SUCCESS
  coverage   INCOMPLETE
  review     REVIEW_REQUIRED
...
WARNING [CDC-003] single-bit clock-domain crossing top.src -> top.q has no recognised synchroniser (1 of 1 bit(s)) [clock relationship UNKNOWN: treated as potentially asynchronous]
```

The strict exit code is 6 (2 for review, 4 for incomplete analysis). Coverage describes how much was evaluated; review describes the findings needing action. In JSON these are `analysis_coverage` and `review_status`. Zero errors does not mean safe: clocks are unknown and some rules were not fully evaluated, including rules outside the selected goal.

Declare the two clocks and their asynchronous relationship:

```sh
sverity --goal cdc --top top --sdc docs/tutorial/clocks.sdc docs/tutorial/bad.sv
```

The report now says `ERROR [CDC-003]`, with `clocks: declared asynchronous`. Exit code 5 combines FAIL (1) and INCOMPLETE (4). SDC declarations are trusted design intent, not independently proved. The advice now asks for destination synchronisation or a justified waiver: adding a clock declaration does not repair this crossing. Without clock intent, advice first asks you to declare it and review the crossing.

```sh
sverity --goal cdc --top top --sdc docs/tutorial/clocks.sdc docs/tutorial/fixed.sv
```

This reports `recognised 2-stage flop synchroniser` and exit 6. The input still needs a timing or CDC contract, and the source must be held long enough to avoid missed pulses. Recognition assumes metastability resolves and the source is glitch-free; it does not establish MTBF or timing closure.

Use `--goal cdc` to focus the run (other rules remain disabled). Explore the results and save full reports:

```sh
sverity --goal cdc --top top --sdc docs/tutorial/clocks.sdc --summary docs/tutorial/bad.sv
sverity --explain CDC-003
sverity --goal cdc --top top --sdc docs/tutorial/clocks.sdc --group --json report.json --html report.html docs/tutorial/bigger.sv
```

`--verbose` expands all evidence, assumptions, occurrences, signals, confidence, proof state, fingerprints and run context. The default labels available launch/capture, clock and path facts and gives each rule's advice once; INFO stays visible on one line. A run with more than 20 findings also names its top rules in the header. Missing source locations are never invented. `--summary` appends per-rule accounting; `--explain` describes the rule. Grouping is useful on larger results; JSON retains individual findings. The last command writes JSON and HTML in the current directory.

A config can bundle settings. This one selects CDC, supplies the SDC relative to itself, and overrides severity:

```sh
sverity --config docs/tutorial/config.json --top top docs/tutorial/bad.sv
```

The finding now reads `WARNING [CDC-003]` and `(was ERROR)` and remains review required. The exit code is 6.

A file waiver or an in-source waiver marks a finding, without proving it:

```sh
sverity --goal cdc --top top --sdc docs/tutorial/clocks.sdc --waivers docs/tutorial/waivers.json docs/tutorial/bad.sv
sverity --goal cdc --top top --sdc docs/tutorial/clocks.sdc docs/tutorial/waived.sv
```

Both show `<WAIVED>` on CDC-003 and exit 6, with advice to retain and review the accepted waiver justification. The source directive precedes the capture procedure; the file waiver narrows by file. Review the actual design and justification before waiving.

Strict exit is the default. For CI that intentionally gates only violations and execution errors, choose:

```sh
sverity --goal cdc --top top --sdc docs/tutorial/clocks.sdc --exit-policy violations docs/tutorial/fixed.sv
```

This exits 0 despite `coverage INCOMPLETE` and outstanding review. That policy gives up using the exit code to gate incomplete analysis and review obligations; inspect the report separately. See [what the analyses establish](rules/ANALYSIS.md) before interpreting a clean result.

A zero-finding run still shows execution, coverage, review and the selected exit policy. When coverage is incomplete and there are no open findings, the console says `NO_OPEN_FINDINGS (coverage incomplete)` rather than a safety verdict. JSON retains its existing review-state enum. `--no-color` disables ANSI colour. Grouped representatives retain advice and key evidence, with exact occurrence and disposition counts; the named JSON or SARIF contains every member.
