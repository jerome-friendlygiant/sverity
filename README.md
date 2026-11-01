# SVerity

An open-source, offline SystemVerilog analyzer for **lint, CDC and RDC**, built natively on
[slang](https://github.com/MikePopoloski/slang) (C++20 CLI + embeddable library). The long-term aim is a transparent,
community-maintained rule deck where nothing is silently hidden and *unknown is never reported as safe*.

**Status: early development, not validated.** Docs: [docs/SVerity_RFC.md](docs/SVerity_RFC.md) (the specification, with its
open issues), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (code map and how to add a rule), [docs/rules/](docs/rules/) (the candidate rule catalog: `catalog.json` plus a table per family).

Per-rule implementation state (executed / partial with its stated gap / not implemented): `sverity --list-rules` and the
`status`/`gap` fields of `docs/rules/catalog.json`.

## Build

```bash
git clone --recursive https://github.com/jerome-friendlygiant/sverity && cd sverity
cmake -S . -B build -G Ninja && cmake --build build -j && ctest --test-dir build
```

Needs CMake ≥ 3.28, a C++20 compiler (tested with GCC 13), and Python 3 at *build time only* (slang's code generator).
slang is a pinned submodule; it fetches its own small dependencies at configure time (set `FETCHCONTENT_SOURCE_DIR_*`
and `FETCHCONTENT_FULLY_DISCONNECTED=ON` for an offline build).

## Use

```bash
build/sverity --top top rtl/*.sv --sdc clocks.sdc [--library cells.json] --json report.json --sarif report.sarif
build/sverity --help
```

Unrecognised options go to slang (`-f`, `-D`, `-I`, `--top`, ...). Exit code is a bit mask: 1 = findings FAIL,
2 = REVIEW_REQUIRED, 4 = analysis INCOMPLETE, 8 = execution error, 64 = usage error.

## Extending

Add a rule by deriving from `sverity::lint::LintRule` (AST rules, see `src/rules/lint/lint_procedural.cpp`) or `sverity::Rule` (model-based) and
registering it with `sverity::registerRule()` (embedders) or one line in `src/rules/registry.cpp` (built-in). Specify it in `docs/rules/<FAMILY>.md`
and give it fixtures in `tests/fixtures/<RULE-ID>/` (annotated SystemVerilog; no C++ needed).

## Acknowledgements

SVerity is built on [slang](https://github.com/MikePopoloski/slang) (MIT, Mike Popoloski): parsing, elaboration, types and the driver/dataflow analysis.
The rule structure draws on [slang-tidy](https://www.sv-lang.com/tools.html) (AST-visitor checks, severity per check) and the netlist/dependency-graph
modelling on [slang-netlist](https://github.com/jameshanlon/slang-netlist) (MIT, James Hanlon). The CDC/RDC rule inventory was cross-checked against
[OpenCDC](https://github.com/Ayoubbelguellaoui/CDC) (Apache-2.0), [RTL Buddy CDC](https://github.com/rtl-buddy/rtl-buddy-cdc) (BSD-3-Clause),
[Verible](https://github.com/chipsalliance/verible) (Apache-2.0), [Verilator](https://verilator.org) and the public AMD/Altera CDC/RDC rule titles.
No code from these projects is included; every check is reimplemented (crosswalks and licence notes: `docs/rules/LNT.md`, `CDC.md`, `RDC.md`).

MIT licence.
