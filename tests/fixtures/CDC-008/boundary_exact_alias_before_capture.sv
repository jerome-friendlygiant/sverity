// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An exact alias before the first capture adds no combinational transform to the launch value.
// expect-none: CDC-008
module top(input logic clka, clkb, d, output logic q);
  logic a_q, alias_q, s1, s2;
  always_ff @(posedge clka) a_q <= d;
  assign alias_q = a_q;
  always_ff @(posedge clkb) begin s1 <= alias_q; s2 <= s1; end
  assign q = s2;
endmodule
