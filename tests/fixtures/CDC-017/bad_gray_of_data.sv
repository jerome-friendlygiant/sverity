// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-none: CDC-033 INFO
// Regression from the independent audit: this hazard must never be classified as safe.
// Gray encoding of arbitrary (non-counter) data: several bits change at once.
module top(input logic clka, clkb, input logic [3:0] din, output logic [3:0] q);
  logic [3:0] g, s1, s2;
  always_ff @(posedge clka) g <= din ^ (din >> 1);
  always_ff @(posedge clkb) begin s1 <= g; s2 <= s1; end
  assign q = s2;
endmodule
