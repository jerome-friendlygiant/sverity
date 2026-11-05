# Contributing to SVerity

The goal is a rule deck people can trust, so a change has to show what it establishes. [AGENTS.md](AGENTS.md) holds the working principles and applies to people as well as agents. Build, layout and how to add a rule are in the [README](README.md#extending).

## Before opening a pull request

`tools/dev/check.sh` must pass. It runs the build, every test and the pinned public-RTL gate.

Fixing a false alarm or a missed defect means keeping the smallest input that reproduces it as a fixture. Widening a recognition means first adding an unsafe lookalike that must stay flagged, and listing it in [`docs/rules/ANALYSIS.md`](docs/rules/ANALYSIS.md).

## Proposing a rule

Open a [rule proposal](.github/ISSUE_TEMPLATE/rule_proposal.md) before writing code. A rule has one predicate, one severity policy and one unknown outcome. Say what it reports, what it exempts, how it decides and what happens when it cannot decide.

A rule moves through these states, recorded in [`docs/rules/catalog.json`](docs/rules/catalog.json):

| State | Means | Needs |
|---|---|---|
| `proposed` | Planned, or a documented subset exists | A `gap` saying what is missing, if any of it runs |
| `implemented` | The whole title is covered | A spec block and a must-not-fire fixture; for CDC and RDC, a mutation pair |
| `validated` | The release stands behind it | Every fixture shape, a written severity policy, and a recorded independent review |

`tools/dev/conformance.py` shows where each rule stands, and `--check` enforces the table in the test suite. Each rule's `deck` field in the catalog is the earliest published deck that holds it (`core`, then `domain`, then `1.0`, each containing the last); a rule leaves only for a written `deck_exclusion` reason, and that decision is reviewed.

An independent review tries to find false-safe results by running the binary on minimal cases. A separate AI reviewer counts, and the release says so: it is not a human sign-off.

## Provenance and licensing

SVerity is MIT licensed, and contributions are accepted under that licence.

- **Never commit** proprietary RTL, vendor-tool output, waiver decks, credentials, personal data, or absolute or local paths.
- **External designs** are referenced by pinned URL and commit, never vendored.
- **Code from another project** needs a compatible licence and an entry in the crosswalk of the relevant family doc, naming the project, licence and what was taken. Nothing is copied without that record.
- **Commercial rule content** stays out. Vendor manuals, rule references and tool output are not sources, including ones that surface on third-party sites. Public vendor pages may inform a rule when cited and paraphrased, as in the [roadmap sources](docs/ROADMAP.md#sources-and-provenance). Comparing against a commercial tool needs legitimate access, and its licence may forbid publishing results.

## Reporting a problem

Bugs and false results go in the issue tracker, with a minimal input if it can be public.
