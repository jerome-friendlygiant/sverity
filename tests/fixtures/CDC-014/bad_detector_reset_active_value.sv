// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Active-high detector flop async-resets to 1 while the source and stages reset to 0.
// expect: CDC-014 WARNING
// expect-confidence: CDC-014 unresolved
// expect-none: CDC-014 INFO
module top(input logic clka, clkb, rst_n, pulse_i, output logic event_o);
  logic toggle_q, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) toggle_q <= 1'b0;
    else if (pulse_i) toggle_q <= ~toggle_q;
  always_ff @(posedge clkb or negedge rst_n) begin
    if (!rst_n) begin
      s1 <= 1'b0; s2 <= 1'b0; s3 <= 1'b0; event_o <= 1'b1;
    end else begin
      s1 <= toggle_q; s2 <= s1; s3 <= s2; event_o <= s2 ^ s3;
    end
  end
endmodule
