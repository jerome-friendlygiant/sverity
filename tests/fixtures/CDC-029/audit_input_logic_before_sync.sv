// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-008 WARNING
// Regression from the independent audit: this hazard must never be classified as safe.
// Two asynchronous primary inputs combined by logic before the synchroniser: glitches are sampled.
module top(input logic clkb, req_a, req_b, output logic q);
  logic s1, s2;
  always_ff @(posedge clkb) begin s1 <= req_a ^ req_b; s2 <= s1; end
  assign q = s2;
endmodule
