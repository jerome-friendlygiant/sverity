// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Regression: `if (flag) q <= const` looks like a synchronous reset but the flag comes from another clock domain.
// The heuristic must never remove that dependency from the crossing analysis.
// expect: CDC-003 ERROR
module top(input logic clka, clkb, d, output logic q);
  logic a_q;
  always @(posedge clka) a_q <= d;
  always @(posedge clkb) if (a_q) q <= 1'b1;
endmodule
