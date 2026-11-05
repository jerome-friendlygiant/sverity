---
name: Rule proposal
about: Propose a new lint, CDC, RDC or constraint rule, or a change to one
labels: rule-proposal
---

**Title.** One line that states exactly what is checked. The title is the contract.

**Family.** `LNT`, `CDC`, `RDC` or `CST`.

**Trigger.** What makes the rule fire, in terms of the design and not a tool's wording.

**Exemptions.** What looks similar but must not fire, and why it is safe.

**Severity.** ERROR is an established violation. WARNING is a potential defect or an unproved property. INFO is context or a recognised structure with stated assumptions. When does each apply?

**Unknown outcome.** What does the rule report when it cannot decide? It must not be silence.

**Minimal examples.** A small design that must fire, and the closest safe lookalike that must not. Only code you may publish.

**Source of the idea.** A public specification, a design you hit it on, or a public reference. Link it. Do not paste vendor manuals, rule references or tool output.

**Existing rules.** Which rule comes closest, and why does this one not belong in it?
