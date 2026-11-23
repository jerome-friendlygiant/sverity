// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Downstream observation register conditionally replaces the detector copy with a constant.
// expect: CDC-014 WARNING
// expect-confidence: CDC-014 unresolved
// expect-none: CDC-014 INFO
module top(input logic clka, clkb, pulse_i, en, output logic event_o);
  logic toggle_q, s1, s2, s3, det;
  always_ff @(posedge clka) if (pulse_i) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb) begin
    s1 <= toggle_q; s2 <= s1; s3 <= s2;
    det <= s2 ^ s3;
    if (en) event_o <= det; else event_o <= 1'b0;
  end
endmodule
