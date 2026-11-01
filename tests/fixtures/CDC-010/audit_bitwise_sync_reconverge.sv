// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-010 WARNING
// Regression from the independent audit: this hazard must never be classified as safe.
module top(input logic clka, clkb, input logic [1:0] d, output logic y);
  logic [1:0] a_q;
  logic s1a, s2a, s1b, s2b;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin s1a <= a_q[0]; s2a <= s1a; s1b <= a_q[1]; s2b <= s1b; end
  always_ff @(posedge clkb) y <= s2a & s2b;
endmodule
