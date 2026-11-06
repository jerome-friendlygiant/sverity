// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/ancestor_chain_bad.sdc
// expect: CST-003 WARNING 4
// expect-count: CST-003 WARNING 1
// revert: ancestor_chain_bad.sdc => ancestor_chain_good.sdc
// revert-expect-none: CST-003
`include "constraint_clock_ports.svh"
