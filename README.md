# SVerity

An open-source, offline SystemVerilog analyzer for **lint, CDC and RDC**, built natively on
[slang](https://github.com/MikePopoloski/slang) (C++20 CLI + embeddable library). The long-term aim is a transparent,
community-maintained rule deck where nothing is silently hidden and *unknown is never reported as safe*.

**Status: early development, not validated.** Docs: [docs/SVerity_RFC.md](docs/SVerity_RFC.md) (the specification and its open issues),
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (design decisions), [docs/rules/](docs/rules/) (`catalog.json` plus a table and spec per family).

Per-rule state (executed / partial with its stated gap / not implemented): `sverity --list-rules`. Library cells (`--library`): kinds in `include/sverity/frontend.h`.

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
[Verible](https://github.com/chipsalliance/verible) (Apache-2.0), [Verilator](https://verilator.org) and the public AMD/Altera CDC/RDC rule titles.
No code from these projects is included; every check is reimplemented (crosswalks and licence notes: `docs/rules/LNT.md`, `CDC.md`, `RDC.md`).

MIT licence.
