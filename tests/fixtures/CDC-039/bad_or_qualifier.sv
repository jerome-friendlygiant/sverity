// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-004 ERROR
// expect-none: CDC-039 INFO
// Regression from the independent audit: this hazard must never be classified as safe.
// Qualifier OR-ed with a free-running local (clkb) enable: capture happens regardless of the synchronised valid.
module top(input logic clka, clkb, v, en, input logic [7:0] d, output logic [7:0] q);
  logic v_a, s1, s2, en_b;
  logic [7:0] d_a;
  always_ff @(posedge clka) begin v_a <= v; d_a <= d; end
  always_ff @(posedge clkb) begin s1 <= v_a; s2 <= s1; en_b <= ~en_b; end
  always_ff @(posedge clkb) if (s2 | en_b) q <= d_a;
endmodule
