# Accuracy suite

Labelled lint, CDC, RDC and SDC-constraint designs for measuring a static analyzer: how often it misses a defect,
raises a false alarm, or calls an undecidable design clean. Score SVerity with `python3 tools/dev/accuracy.py build/sverity`.

Each case is a directory `cases/<FAMILY>/<name>/` with `design.sv` (top module `top`), an optional `constraints.sdc`
and `label.json`. Read any label for its fields. The verdict is `bad` (a defect at `sites`), `good` (nothing to report)
or `unknown` (the RTL and constraints cannot decide; a tool should say so).

The few rules that keep it worth measuring:

- **Measure before fixing.** Author and label each new batch before running SVerity on it. After its first score,
  keep it as regression evidence; a fix also gets a minimal case in `tests/fixtures/`. Measure accuracy again on fresh cases.
- **Labelled by the design.** Authors and labellers do not run SVerity or read its rules, and labels never name rule IDs.
- **Two readings.** A second labeller labels each case blind; a disagreement is settled by a third reading
  (`settled_by`).
- **Adapted code keeps its attribution.** A case taken from open-source RTL keeps only the part it needs, a header with
  the source path, commit, copyright and licence, and a `provenance` entry in its label.
- **In scope.** A case concerns something the [RFC](../../docs/SVerity_RFC.md) says the tool checks.
