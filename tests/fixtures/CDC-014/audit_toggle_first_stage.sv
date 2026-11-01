// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-007 ERROR
// expect-none: CDC-014 INFO
// Regression from the independent audit: this hazard must never be classified as safe.
// Toggle synchroniser whose edge detector XORs the FIRST (metastable) stage with stage 2.
module top(input logic clka, clkb, ev, output logic pulse);
  logic t_a, s1, s2;
  always_ff @(posedge clka) if (ev) t_a <= ~t_a;
  always_ff @(posedge clkb) begin s1 <= t_a; s2 <= s1; end
  assign pulse = s1 ^ s2;
endmodule
