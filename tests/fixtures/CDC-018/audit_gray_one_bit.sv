// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-none: CDC-033 INFO
// Regression from the independent audit: this hazard must never be classified as safe.
// Only bit 0 of g is an adjacent-bit XOR; bits 3:1 are arbitrary data. Whole vector treated as Gray.
module top(input logic clka, clkb, input logic [3:0] din, output logic [3:0] q);
  logic [3:0] x, g;
  logic [3:0] gq, s1, s2;
  always_ff @(posedge clka) x <= din;
  assign g[0] = x[1] ^ x[0];
  assign g[3:1] = x[3:1];
  always_ff @(posedge clka) gq <= g;
  always_ff @(posedge clkb) begin s1 <= gq; s2 <= s1; end
  assign q = s2;
endmodule
