// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/physically_exclusive_bad.sdc
// expect: CST-003 WARNING 3
// expect-count: CST-003 WARNING 1
// revert: physically_exclusive_bad.sdc => physically_exclusive_good.sdc
// revert-expect-none: CST-003
`include "constraint_clock_ports.svh"
