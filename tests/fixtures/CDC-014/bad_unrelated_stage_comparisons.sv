// Each adjacent stage is compared with a different input; these comparisons do not detect a transition between stages.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-014 WARNING
// expect-confidence: CDC-014 unresolved
module top(input logic clka, clkb, a, b, pulse_i, output logic pulse_o);
  logic toggle_q, s1, s2, s3;
  always_ff @(posedge clka) if (pulse_i) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb) begin
    s1 <= toggle_q; s2 <= s1; s3 <= s2;
    if ((s2 == a) && (s3 == b)) pulse_o <= 1'b1;
    else pulse_o <= 1'b0;
  end
endmodule
