// The outer XOR cancels the matching subtree, leaving only s3.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-014 WARNING
// expect-confidence: CDC-014 unresolved
module top(input logic clka, clkb, pulse_i, output logic event_o);
  logic toggle_q, s1, s2, s3;
  always_ff @(posedge clka) if (pulse_i) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb) begin s1 <= toggle_q; s2 <= s1; s3 <= s2; end
  assign event_o = (s2 ^ s3) ^ s2;
endmodule
