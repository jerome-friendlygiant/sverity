// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/source_override_bad.sdc
// expect: CST-003 WARNING 2
// expect-count: CST-003 WARNING 1
// revert: source_override_bad.sdc => source_override_good.sdc
// revert-expect-none: CST-003
// expect: CST-004 WARNING
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk, other, d, output logic a, b);
  always_ff @(posedge clk) a <= d;
  always_ff @(posedge other) b <= a;
endmodule
