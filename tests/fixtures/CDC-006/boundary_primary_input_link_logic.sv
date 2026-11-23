// sverity-args: --top top
// The primary-input path check also treats logic between its two sampling stages as a broken link.
// expect: CDC-006 WARNING
// expect-unresolved: CDC-006
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clkb, d, output logic q);
  logic s1, s2;
  always_ff @(posedge clkb) begin s1 <= d; s2 <= ~s1; end
  assign q = s2;
endmodule
