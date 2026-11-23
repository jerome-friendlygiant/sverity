// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Uncontracted top-level output carrying a synchronized CDC value must emit CST-012 WARNING Unresolved and INCOMPLETE.
// expect: CST-012 WARNING
// expect-unresolved: CST-012
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  logic a_q, s1, s2;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin s1 <= a_q; s2 <= s1; end
  assign q = s2;
endmodule
