// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/missing_master_bad.sdc
// expect: CST-002 WARNING 2
// expect-count: CST-002 WARNING 1
// expect-coverage: constraints INCOMPLETE
// revert: missing_master_bad.sdc => missing_master_good.sdc
// revert-expect-none: CST-002
// expect: CST-004 WARNING
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk, div, d, output logic launch, capture);
  always_ff @(posedge clk) launch <= d;
  always_ff @(posedge div) capture <= launch;
endmodule
