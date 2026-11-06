# Verification

How we show SVerity is good enough for a company to sign off with it. Each test layer answers one question an adopter
asks; none can stand in for another.

| Layer | Question it answers | Gate |
|---|---|---|
| `tests/unit/` | Can I trust what the report says? | Every clause of the [diagnostic contract](SVerity_RFC.md#3-diagnostic-contract-non-negotiable) has a test |
| `tests/fixtures/` | Does each rule do what its spec says? | Every rule in a release deck is `validated` (`tools/dev/conformance.py`) |
| `tests/accuracy/` (golden set) | Is it right about the hardware? | No critical defect missed or undecidable case called clean; noise within budget per family |
| `tests/public_projects/` | Does it work on real code at real scale? | Real designs elaborate and finish within budget; no false ERROR on known-good designs |

The last question, whether it works on a company's own designs against the tool it uses today, is answered by a pilot,
outside this repository. These layers exist so a pilot starts from evidence.

## Principles

- **Labels describe the hardware, not the tool.** Golden-set labels say what is true of the RTL and constraints. They
  never name a rule, and their authors and labellers do not run SVerity on the cases they label.
- **Two readings.** A second labeller reads each case blind; a disagreement goes to a third reading. Agreement on the
  verdict but not on the defect counts as a disagreement.
- **Score a new batch before fixing anything.** That first score is the honest measurement. Afterwards the cases are an
  ordinary regression set that anyone may read.
- **A golden failure becomes a fixture.** When a case exposes a gap, the fix gets a minimal fixture, so rule specs
  absorb outside evidence instead of only the examples their authors thought of.
- **Unknown is never clean.** A case the RTL cannot decide must be disclosed, and counts as a miss if it is not.
- **Fixtures are not the measure of accuracy.** They are written with the rules and test the spec, not whether the spec
  is right.

## Where it is weak, and where it goes next

- **Golden set:** reduced designs labelled by AI models; some cases repeat a constraint or synchronization mechanism. Grow it towards coverage of each family's defect classes,
  with common and edge cases and safe lookalikes. Have engineers review a sample of labels. Add real bug fixes from
  public history as before/after pairs.
- **Public projects:** known-good designs only, and no measure of review effort. Budget findings per design, and finish a
  full SoC (`top_earlgrey`).
- **Fixtures:** no rule is `validated` yet; the core deck comes first.
- **Unit:** tests exist, but nothing shows which contract clause each covers.
- **Releases:** each release should publish one qualification report from all four layers: results, changes since the
  last release and known limitations.
