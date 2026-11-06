// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/period_override_bad.sdc
// expect: CST-003 WARNING 2
// expect-count: CST-003 WARNING 1
// revert: period_override_bad.sdc => period_override_good.sdc
// revert-expect-none: CST-003
`include "constraint_clock_ports.svh"
