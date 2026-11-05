// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-028 ERROR
// expect-none: CDC-039 INFO
// Regression from the independent audit: this hazard must never be classified as safe.
// Payload is captured unconditionally every clkb cycle; the synchronised valid only clears it.
module top(input logic clka, clkb, v, input logic [7:0] d, output logic [7:0] q);
  logic v_a, s1, s2;
  logic [7:0] d_a;
  always_ff @(posedge clka) begin v_a <= v; d_a <= d; end
  always_ff @(posedge clkb) begin s1 <= v_a; s2 <= s1; end
  always_ff @(posedge clkb) begin
    q <= d_a;              // raw, unqualified multi-bit capture
    if (s2) q <= '0;       // synchronised control only clears
  end
endmodule
