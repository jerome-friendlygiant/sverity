// sverity-args: --top top --sdc @DIR@/valid_exception.sdc
// Boundary: path-exception selectors that resolve to actual design objects emit no CST-002 warning.
// expect-none: CST-002
module top(input logic clka, clkb, d, output logic q);
  logic a_q;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) q <= a_q;
endmodule
