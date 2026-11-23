// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Mutually exclusive packed comparisons in the qualifier suppress the edge detector.
// expect: CDC-014 WARNING
// expect-confidence: CDC-014 unresolved
// expect-none: CDC-014 INFO
module top(input logic clka, clkb, en, sel, output logic event_o);
  logic toggle_q, s1, s2, s3;
  always_ff @(posedge clka) if (en) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb) begin
    s1 <= toggle_q;
    s2 <= s1;
    s3 <= s2;
  end
  wire [1:0] pair = {sel, en};
  always_ff @(posedge clkb) event_o <= (s2 ^ s3) && ((pair == 2'b10) && (pair == 2'b01));
endmodule
