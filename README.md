# SVerity

An open-source, offline SystemVerilog analyzer for **lint, CDC and RDC**, built natively on
[slang](https://github.com/MikePopoloski/slang) (C++20 CLI + embeddable library). The long-term aim is a transparent,
community-maintained rule deck where nothing is silently hidden and *unknown is never reported as safe*.

**Status: early development, not validated.** Docs: [docs/SVerity_RFC.md](docs/SVerity_RFC.md) (the specification, with its
open issues), [docs/rules/](docs/rules/) (the 184 candidate rules: `catalog.json` plus a table per family).
Per-rule state: `sverity --list-rules`.

| Progress | Rules |
|---|---|
| Fully implemented | 8 |
| Partial (the catalog's `gap` field says what is missing) | 41 |
| Not implemented | 135 |

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
build/sverity --top top rtl/*.sv --sdc clocks.sdc --json report.json --sarif report.sarif
build/sverity --help
```

Unrecognised options go to slang (`-f`, `-D`, `-I`, `--top`, ...). Exit code is a bit mask: 1 = findings FAIL,
2 = REVIEW_REQUIRED, 4 = analysis INCOMPLETE, 8 = execution error, 64 = usage error.

MIT licence.
