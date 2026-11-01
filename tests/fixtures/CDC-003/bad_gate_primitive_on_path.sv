// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// AUDIT t3: a gate primitive between the domains must not make the crossing disappear.
// expect: CDC-003 ERROR
// expect: CDC-008 WARNING
module top(input logic clka, clkb, d, x, output logic q);
  logic a_q, y;
  always @(posedge clka) a_q <= d;
  and g1(y, a_q, x);
  always @(posedge clkb) q <= y;
endmodule
