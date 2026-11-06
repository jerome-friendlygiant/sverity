// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: if (b_clear) begin => if (a_clear) begin
// revert-expect-none: RDC-023 WARNING
// The receiver of a toggle synchroniser is cleared by its own reset: clearing it alone compares the held toggle with a cleared copy.
// expect: RDC-023 WARNING
module top(input logic clka, clkb, a_clear, b_clear, send, output logic event_b);
  logic toggle_a = 1'b0, s1, s2, s3;
  always_ff @(posedge clka) if (a_clear) toggle_a <= 1'b0; else if (send) toggle_a <= ~toggle_a;
  always_ff @(posedge clkb) begin
    if (b_clear) begin s1 <= 1'b0; s2 <= 1'b0; s3 <= 1'b0; event_b <= 1'b0; end
    else begin s1 <= toggle_a; s2 <= s1; s3 <= s2; event_b <= s2 ^ s3; end
  end
endmodule
