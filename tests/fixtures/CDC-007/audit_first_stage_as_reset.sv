// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-007 ERROR
// Regression from the independent audit: this hazard must never be classified as safe.
// First synchroniser stage also drives the asynchronous reset of another clkb register.
module top(input logic clka, clkb, d, x, output logic q, r);
  logic a_q, s1, s2;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin s1 <= a_q; s2 <= s1; end
  always_ff @(posedge clkb or posedge s1) if (s1) r <= 1'b0; else r <= x;
  assign q = s2;
endmodule
