// sverity-args: --top top --sdc @DIR@/clock_group_port_selector.sdc
// expect: CST-018 WARNING
// expect-unresolved: CST-018
// expect-coverage: constraints INCOMPLETE
module top(input logic clk, B, d, output logic q);
  logic r;
  always_ff @(posedge clk) r <= d;
  always_ff @(posedge B) q <= r;
endmodule
