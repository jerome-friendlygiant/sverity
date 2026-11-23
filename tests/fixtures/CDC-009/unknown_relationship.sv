// sverity-args: --top top
// Independently synchronized bus bits are visible, but no clock constraints establish their relationship.
// expect: CDC-009 WARNING
// expect-unresolved: CDC-009
// expect-none: CDC-009 ERROR
// expect-coverage: clock-relationships INCOMPLETE
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d0, d1, output logic [1:0] q);
  logic [1:0] a_q, s1, s2;
  always_ff @(posedge clka) a_q <= {d1, d0};
  always_ff @(posedge clkb) begin s1 <= a_q; s2 <= s1; end
  assign q = s2;
endmodule
