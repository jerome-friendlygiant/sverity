// The qualifier aliases the stage transition and suppresses the event for every binary pair.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-014 WARNING
// expect-confidence: CDC-014 unresolved
// expect-none: CDC-014 INFO
module top(input logic clka, clkb, pulse_i, output logic event_o);
  logic toggle_q, s1, s2, s3;
  wire changed = (s2 == s3);
  always_ff @(posedge clka) if (pulse_i) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb) begin
    s1 <= toggle_q; s2 <= s1; s3 <= s2;
    event_o <= changed && (s2 != s3);
  end
endmodule
