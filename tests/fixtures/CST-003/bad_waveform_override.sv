// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/waveform_override_bad.sdc
// expect: CST-003 WARNING 2
// expect-count: CST-003 WARNING 1
// revert: waveform_override_bad.sdc => waveform_override_good.sdc
// revert-expect-none: CST-003
`include "constraint_clock_ports.svh"
