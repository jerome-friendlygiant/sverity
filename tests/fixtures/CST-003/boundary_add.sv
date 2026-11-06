// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/boundary_add.sdc
// -add suppresses the same-name override warning and permits another clock on a port.
// expect-none: CST-003
`include "constraint_clock_ports.svh"
