// sverity-args: --top top --goal cdc --sdc @DIR@/virtual_launch_unknown.sdc
// expect-unresolved: CST-004
// expect-unresolved: CDC-029
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk, e, output logic q);
  always_ff @(posedge clk) q <= e;
endmodule
