# AGENTS.md

Repo conventions (build, layout, how to add a rule, docs): see README.md and docs/ARCHITECTURE.md. This file holds what is not obvious from them.

## Hard rules
- Public repo: never commit proprietary RTL, vendor-tool output, credentials, absolute/local paths or personal data. External designs are referenced by pinned URL + commit, never vendored. No code copied from other projects without provenance recorded in the family doc's crosswalk.
- Unknown or unsupported is never "safe": warn, mark the scope INCOMPLETE, never drop or hide a finding.
- `docs/rules/catalog.json` is the sign-off list: `implemented` only if the whole rule title is covered, otherwise Partial with a `gap`; `validated` needs a written severity policy and independent review. Never restate a status or gap in prose.
- Commit small logical steps; push only your assigned branch; open PRs only when asked. Commit messages must not contain `Co-Authored-By` or session links.

## Verifying a change
`tools/dev/check.sh` is the one verification command: build, ctest, then the pinned public-RTL gate, stopping at the first failure. Do not commit unless it passes. Read `docs/rules/ANALYSIS.md` for the analysis you are changing instead of rediscovering it from the code.

## Working principles (learnt on this project)
- **Fix the shared cause first.** When many rules are Partial for the same reason, change the shared analysis or model, then re-express the rules on it. Do not patch rules one by one.
- **Port behind an oracle.** Move a rule onto a new analysis with its existing fixtures as the regression guard. If a fixture now disagrees, decide which is right under the new semantics and change the fixture with a stated reason; never loosen a check to make a fixture pass.
- **The title is the contract.** Before calling a rule done, read its title against what the code reports. Split bundled titles, merge rules with the same predicate, retitle to the predicate. Change code, catalog, doc table and fixtures in the same commit; keep every commit green.
- **Approximation must surface.** If an analysis cannot decide (loops walked once, opaque calls, unsupported constructs), the rule reports Unresolved. "No finding" is only allowed when the analysis proved absence.
- **Zero errors is a claim, not a result.** Never accept "no error" or a falling count as success on its own. Read the design and decide for yourself whether it violates the rule, then compare with what the tool says; a finding that disappears needs a reason. Before widening a recognition, build unsafe lookalikes that it must keep flagging, and fix false-safe results before false alarms. Scope each check to what its logic really establishes, and write that logic, its assumptions and its prerequisites down in `docs/rules/ANALYSIS.md`.
- **Verify against the running tool, not prose.** Code beats comments, comments beat old docs. Reproduce a claimed defect with a minimal input before fixing it; keep that input as a fixture.
- **Independent adversarial review beats self-review.** After a substantial change, have a separate reviewer hunt false-safe results by running the binary on minimal cases. Fix what is confirmed, keep one small fixture each, record what remains approximate. Use the strongest model for review and judgement, a cheaper one for bounded mechanical work with precise instructions and the code as source of truth; check delegated output (run the tests, spot-read) before committing it.
- **Delegation hygiene.** Never run two builds in one build directory; do not let two agents edit the same file; agents must not commit or push on your behalf.
- **Tests stay few and meaningful.** One defect per bad fixture; a good, boundary and unknown case per rule; no scratch inputs, no duplicates of another fixture's body, names that say what is tested (not who found it). Editing a fixture header shifts expected line numbers.
- **Docs hold principles and contracts, not code restated.** If the code, a test or the catalog says it, the doc does not. Specs state trigger, exemptions, severity and unknown outcome; design pages state decisions and why.
- **Report honestly.** Say what ran and what did not, give counts from the tool, and flag decisions that belong to the user instead of settling them silently.
