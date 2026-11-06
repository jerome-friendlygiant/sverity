// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/generated_override_bad.sdc
// expect: CST-003 WARNING 3
// expect-count: CST-003 WARNING 1
// revert: generated_override_bad.sdc => generated_override_good.sdc
// revert-expect-none: CST-003
`include "constraint_clock_ports.svh"
