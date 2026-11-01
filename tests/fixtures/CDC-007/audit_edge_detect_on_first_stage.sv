// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-007 ERROR
// Regression from the independent audit: this hazard must never be classified as safe.
// First (metastable) stage feeds functional logic, but that logic also reads stage 2.
module top(input logic clka, clkb, d, output logic y);
  logic a_q, s1, s2;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin s1 <= a_q; s2 <= s1; end
  assign y = s1 & s2;      // s1 used functionally
endmodule
