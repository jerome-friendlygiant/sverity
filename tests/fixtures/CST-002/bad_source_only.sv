// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/source_only_bad.sdc
// expect: CST-002 WARNING 2
// expect-count: CST-002 WARNING 1
// expect-coverage: constraints INCOMPLETE
// revert: source_only_bad.sdc => source_only_good.sdc
// revert-expect-none: CST-002
`include "constraint_clock_ports.svh"
