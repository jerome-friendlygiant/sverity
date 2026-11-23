// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Self-canceling wire (en ^ en) folds to constant zero and suppresses the edge detector.
// expect: CDC-014 WARNING
// expect-confidence: CDC-014 unresolved
// expect-none: CDC-014 INFO
module top(input logic clka, clkb, input bit en, output logic event_o);
  logic toggle_q, s1, s2, s3;
  always_ff @(posedge clka) if (en) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb) begin
    s1 <= toggle_q;
    s2 <= s1;
    s3 <= s2;
  end
  wire zer = en ^ en;
  always_ff @(posedge clkb) event_o <= (s2 ^ s3) && zer;
endmodule
