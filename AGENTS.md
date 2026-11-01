# AGENTS.md
- Public repo: never commit proprietary RTL, vendor-tool output, credentials, absolute/local paths or personal data. External designs are referenced by pinned URL + commit (`tests/public_projects`), never vendored.
- `docs/SVerity_RFC.md` is the spec and `docs/ARCHITECTURE.md` the code map: edit them in place. Keep docs minimal and non-duplicating (code, tests and `catalog.json` are the source of truth; never restate a gap or status in prose). Add a doc file only when it replaces text that would otherwise be duplicated; no CI or CONTRIBUTING unless asked. Not accepting outside contributions yet.
- `docs/rules/catalog.json` is the sign-off list: `implemented` only if the whole rule title is covered, otherwise the rule is Partial with a `gap`; `validated` needs a written severity policy and independent review.
- Unknown or unsupported is never "safe": warn, mark the scope INCOMPLETE, never drop or hide a finding.
- Commit small logical steps; push only your assigned branch; open PRs only when asked. Commit messages must not contain `Co-Authored-By` or session links.
- Subagents sparingly: opus for review/audit, haiku for bounded parallel work; do small tasks inline.
- First slang build takes ~10 min on 4 cores; never run two builds in the same build directory.
