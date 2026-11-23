// sverity-args: --top top --sdc @DIR@/unknown_period.sdc
// The combinational event source has no pulse-width or clock-period contract.
// expect: CDC-013 WARNING
// expect-confidence: CDC-013 unresolved
module top(input logic clka, clkb, x, y, output logic s1, s2);
  logic event_q;
  always_ff @(posedge clka) event_q <= x & ~y;
  always_ff @(posedge clkb) begin s1 <= event_q; s2 <= s1; end
endmodule
