// sverity-args: --top top --sdc @DIR@/variables_unknown.sdc
// A failed declaration leaves the crossing unresolved even with a clock group.
// expect: CDC-003 WARNING
// expect: CST-004 WARNING
// expect: CST-011 WARNING
// expect: CST-018 WARNING
// expect-coverage: clock-relationships INCOMPLETE
`include "variable_crossing.svh"
