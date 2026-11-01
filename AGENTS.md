# AGENTS.md
- Public repo: never commit proprietary RTL, vendor-tool output, credentials, absolute/local paths or personal data. External designs are referenced by pinned URL + commit (`tests/public_projects`), never vendored.
- `docs/SVerity_RFC.md` is the spec: edit it in place. Keep docs minimal (code and tests are the source of truth); no CI, CONTRIBUTING or new doc files unless asked. Not accepting outside contributions yet.
- `docs/rules/catalog.json` is the sign-off list: `implemented` only if the whole rule title is covered, otherwise the rule is Partial with a `gap`; `validated` needs a written severity policy and independent review.
- Unknown or unsupported is never "safe": warn, mark the scope INCOMPLETE, never drop or hide a finding.
- Commit small logical steps; push only your assigned branch; open PRs only when asked. Commit messages must not contain `Co-Authored-By` or session links.
- Subagents sparingly: opus for review/audit, haiku for bounded parallel work; do small tasks inline.
- First slang build takes ~10 min on 4 cores; never run two builds in the same build directory.
