// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Regression: the `iff` qualifier of an event control is an enable dependency.
// expect: CDC-028 ERROR
module top(input logic clka, clkb, d, output logic q);
  logic a_q;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb iff a_q) q <= ~q;
endmodule
