// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/logically_exclusive_bad.sdc
// expect: CST-003 WARNING 3
// expect-count: CST-003 WARNING 1
// revert: logically_exclusive_bad.sdc => logically_exclusive_good.sdc
// revert-expect-none: CST-003
`include "constraint_clock_ports.svh"
