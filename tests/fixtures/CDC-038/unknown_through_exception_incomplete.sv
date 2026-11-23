// sverity-args: --top top --sdc @DIR@/through_net.sdc
// A path exception using -through is not path-evaluated and must mark constraints INCOMPLETE.
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  logic a_q, mid;
  always_ff @(posedge clka) a_q <= d;
  assign mid = a_q;
  always_ff @(posedge clkb) q <= mid;
endmodule
