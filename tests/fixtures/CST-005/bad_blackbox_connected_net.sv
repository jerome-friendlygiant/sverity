// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --ignore-unknown-modules
// Regression: a net that also touches a black box must not stop the crossing analysis.
// expect: CDC-003 ERROR
// expect: CST-005 WARNING
// expect-coverage: blackboxes INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  logic a_q;
  wire w;
  always @(posedge clka) a_q <= d;
  assign w = a_q;
  bbmod u_bb(.x(w));
  always @(posedge clkb) q <= w;
endmodule
