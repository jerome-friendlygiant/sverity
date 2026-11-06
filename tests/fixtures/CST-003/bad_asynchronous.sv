// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/asynchronous_bad.sdc
// expect: CST-003 WARNING 3
// expect-count: CST-003 WARNING 1
// revert: asynchronous_bad.sdc => asynchronous_good.sdc
// revert-expect-none: CST-003
`include "constraint_clock_ports.svh"
