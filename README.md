<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logo/sverity-logo-dark.svg">
    <img alt="SVerity" src="docs/logo/sverity-logo.svg" width="320">
  </picture>
</h1>

An open-source, offline SystemVerilog analyzer for **lint, CDC and RDC**, built natively on
[slang](https://github.com/MikePopoloski/slang) (C++20 CLI + embeddable library). The long-term aim is a transparent,
community-maintained rule deck where nothing is silently hidden and *unknown is never reported as safe*.

**Status: early development, not validated.** Docs: [docs/SVerity_RFC.md](docs/SVerity_RFC.md) (the specification), [docs/ROADMAP.md](docs/ROADMAP.md) (open work),
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (design decisions), [docs/rules/](docs/rules/) (`catalog.json` plus a table and spec per family).

Per-rule state (executed / partial with its stated gap / not implemented): `sverity --list-rules`. Library cells (`--library`): kinds in `include/sverity/frontend.h`.

## Build

```bash
git clone --recursive https://github.com/jerome-friendlygiant/sverity && cd sverity
cmake -S . -B build -G Ninja && cmake --build build -j && ctest --test-dir build
```

`tools/dev/check.sh` is the one verification command. `tools/dev/conformance.py` shows how far each rule is from `validated`, and `tools/dev/capacity.py` measures the OpenTitan capacity target.

Needs CMake ≥ 3.28, a C++20 compiler (tested with GCC 13), and Python 3 at *build and test time only* (slang's code generator, two test scripts).
slang is a pinned submodule; it fetches its own small dependencies at configure time (set `FETCHCONTENT_SOURCE_DIR_*`
and `FETCHCONTENT_FULLY_DISCONNECTED=ON` for an offline build).

## Use

```bash
build/sverity --top top rtl/*.sv --sdc clocks.sdc [--library cells.json] --json report.json --sarif report.sarif
build/sverity --help
```

Unrecognised options go to slang (`-f`, `-D`, `-I`, `--top`, ...). Exit code is a bit mask: 1 = findings FAIL,
2 = REVIEW_REQUIRED, 4 = analysis INCOMPLETE, 8 = execution error, 64 = usage error.

A JSON configuration can set rule options, severity overrides and enabled rules:

```json
{
  "options": {"CDC-005.min_stages": 3, "LNT-036.report_modular": true},
  "severity": {"CDC-005": "WARNING"},
  "enable": ["CDC-*", "CST-*"],
  "disable": ["CDC-035"],
  "sdc": ["constraints/clocks.sdc"]
}
```

Use `--config settings.json`. The optional `sdc`, `library` and `waivers` arrays contain paths relative to the configuration file. The `enable` array narrows the enabled set; within the file, `disable` wins. CLI `--enable-rule` can re-enable file-disabled rules, and CLI `--disable-rule` wins last. CLI `--option KEY=VALUE` and `--severity PATTERN=INFO|WARNING|ERROR` override file values; matching severity patterns apply in file order, then CLI order. CLI path options replace their corresponding file arrays. Unmatched patterns and invalid options are usage errors. Unresolved findings, unknown proofs and obligations have a WARNING floor; FATAL, slang errors and invalid-input errors cannot be downgraded. Original and requested severities remain visible in JSON and SARIF. A downgraded ERROR remains REVIEW_REQUIRED; strict exit retains the review bit, while `--exit-policy violations` gates only remaining effective ERRORs and execution errors.

Use `--goal lint`, `--goal cdc` or `--goal rdc` to scope a run; repeat the option to select a union. A config file may instead contain `"goal": ["cdc", "rdc"]`; CLI goals replace that array. Goal selection comes first, then file `enable`/`disable`, then CLI `--enable-rule`/`--disable-rule`. An explicit enable can restore a rule outside the goal. Other rules remain DISABLED and coverage stays INCOMPLETE. Slang diagnostics remain visible under every goal, even when their mapped lint rules are disabled. The summary and report inputs identify scoped runs.

Use `--deck core`, `--deck domain` or `--deck 1.0` to report coverage for a published deck ([what each holds](docs/rules/catalog.json), [where it stands](tools/dev/conformance.py)). Rules outside it still run and keep their findings, listed as `preview`, but the run claims nothing about them. A deck run reports COMPLETE only if every rule of the deck executed in full and no analysis scope was limited.

Use `--group --json report.json` (or `--sarif report.sarif`) for a grouped console view; the announcement names the complete report, whose `root_cause` fields link individual findings. Without a successfully written report, the console remains complete. ERROR and FATAL groups appear first, including when `--max-console` limits other groups.

Use `// sverity-waive RULE owner="team" reason="reviewed justification"` before a finding’s reported line, or `--waivers file.json`; waived findings stay visible.
Generate editable entries with `--waiver-template file.json` (placeholders are rejected); use `--no-source-waivers` to review with source waivers disabled. See RFC §3 for exact reach and syntax.

## Extending

A built-in rule is an AST rule (`sverity::lint::LintRule`, one class and one `ASTVisitor`; see `src/rules/lint/lint_procedural.cpp`), a rule on the
procedure analysis (`src/rules/lint/lint_dataflow.cpp`) or a model-based `sverity::Rule`; register it with one line in its family factory
(`src/rules/registry.cpp` lists them). Embedders register their own with `sverity::registerRule()` (a rule outside the catalog is listed as `custom`).
In one commit:

1. the entry in `docs/rules/catalog.json` and its row in `docs/rules/<FAMILY>.md` (titles must match), `status: implemented` only if the whole
   title is covered, otherwise a `gap`; the unit tests check catalog, docs and the registered rules agree;
2. the rule's spec block in the family doc: trigger, exemptions, severity, unknown outcome;
3. fixtures in `tests/fixtures/<RULE-ID>/` (annotated SystemVerilog, no C++; format at the top of `tests/unit/fixture_runner.cpp`): good, bad, boundary,
   hierarchy/unknown, and for CDC/RDC a mutation (`// revert:`).

## Acknowledgements

SVerity is built on [slang](https://github.com/MikePopoloski/slang) (MIT, Mike Popoloski): parsing, elaboration, types and the driver/dataflow analysis.
The rule structure draws on [slang-tidy](https://www.sv-lang.com/tools.html) (AST-visitor checks, severity per check) and the netlist/dependency-graph
modelling on [slang-netlist](https://github.com/jameshanlon/slang-netlist) (MIT, James Hanlon). The CDC/RDC rule inventory was cross-checked against
[OpenCDC](https://github.com/Ayoubbelguellaoui/CDC) (Apache-2.0), [RTL Buddy CDC](https://github.com/rtl-buddy/rtl-buddy-cdc) (BSD-3-Clause),
[Verible](https://github.com/chipsalliance/verible) (Apache-2.0), [Verilator](https://verilator.org), and against the public AMD and Altera rule lists.
No code from these projects is included; every check is reimplemented (crosswalks and licence notes: `docs/rules/LNT.md`, `CDC.md`, `RDC.md`).

MIT licence. To contribute or report a problem, see [CONTRIBUTING.md](CONTRIBUTING.md).

Use `sverity --explain CDC-003` for an offline rule description and `--summary` for per-rule run counts.

Use `--html <file>` to write a self-contained HTML report alongside JSON or SARIF, and `--timings <file>` to record time and peak memory per phase and rule (it varies between runs, so it is kept out of the report).

See the [tutorial](docs/TUTORIAL.md), and the [roadmap](docs/ROADMAP.md) for open work, future goals and [moving from a commercial flow](docs/ROADMAP.md#5-coming-from-a-commercial-flow).
