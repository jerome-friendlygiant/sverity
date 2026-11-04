// sverity-args: --top top --sdc @DIR@/variables_async.sdc
// Valid variable/loop declarations establish both clocks and the async relationship.
// expect: CDC-003 ERROR
// expect-none: CST-004
// expect-none: CST-011
// expect-none: CST-009
// expect-none: CST-018
`include "variable_crossing.svh"
