// Inverted AND of adjacent stages is a supported edge detector.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-014 INFO
// expect-none: CDC-014 WARNING
// expect-count: CDC-024 WARNING 1
module top(input logic clka, clkb, pulse_i, output logic event_o);
  logic toggle_q, s1, s2, s3;
  always_ff @(posedge clka) if (pulse_i) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb) begin
    s1 <= toggle_q; s2 <= s1; s3 <= s2;
    if (s2 & ~s3) event_o <= 1'b1;
    else event_o <= 1'b0;
  end
endmodule
