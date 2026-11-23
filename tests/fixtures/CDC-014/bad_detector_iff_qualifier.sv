// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Event-control iff qualifier on the detector flop prevents unconditional destination sampling.
// expect: CDC-014 WARNING
// expect-confidence: CDC-014 unresolved
// expect-none: CDC-014 INFO
module top(input logic clka, clkb, pulse_i, output logic event_o);
  logic toggle_q, s1, s2, s3;
  always_ff @(posedge clka) if (pulse_i) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb) begin
    s1 <= toggle_q;
    s2 <= s1;
    s3 <= s2;
  end
  always_ff @(posedge clkb iff 1'b0) event_o <= s2 ^ s3;
endmodule
