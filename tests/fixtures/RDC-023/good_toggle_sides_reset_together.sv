// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One asynchronous reset clears both sides of the toggle synchroniser: a stated trust model, not an obligation.
// expect: RDC-023 INFO
// expect-none: RDC-023 WARNING
module top(input logic clka, clkb, rst_n, send, output logic event_b);
  logic toggle_a, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) toggle_a <= 1'b0; else if (send) toggle_a <= ~toggle_a;
  always_ff @(posedge clkb or negedge rst_n) begin
    if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; s3 <= 1'b0; event_b <= 1'b0; end
    else begin s1 <= toggle_a; s2 <= s1; s3 <= s2; event_b <= s2 ^ s3; end
  end
endmodule
