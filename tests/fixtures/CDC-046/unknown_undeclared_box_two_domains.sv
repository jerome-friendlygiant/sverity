// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// an undeclared module (black box) receives signals from two clock domains: which crossing it implements is unknown, never safe
// expect: CDC-046 WARNING
// expect-coverage: blackboxes INCOMPLETE
module top(input logic clka, clkb, d, output logic o);
  logic a_q, b_q;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) b_q <= d;
  unknown_ip u(.x(a_q), .y(b_q), .z(o));
endmodule
