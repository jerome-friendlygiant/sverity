// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// Regression from the independent audit: this hazard must never be classified as safe.
// A 4-bit destination bus synchronised as a vector, launched from four separate 1-bit clka registers
// (e.g. one-hot-unsafe encoded state): incoherent capture, but each bit is its own "single-bit" group.
module top(input logic clka, clkb, input logic [3:0] d, output logic [3:0] q);
  logic a0, a1, a2, a3;
  logic [3:0] s1, s2;
  always_ff @(posedge clka) begin a0 <= d[0]; a1 <= d[1]; a2 <= d[2]; a3 <= d[3]; end
  always_ff @(posedge clkb) begin s1 <= {a3, a2, a1, a0}; s2 <= s1; end
  assign q = s2;
endmodule
