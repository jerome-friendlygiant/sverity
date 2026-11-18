// sverity-args: --top top
// The inverted clock is declared with its value (`wire c = ~slow;`) instead of assigned: a register on it that samples the parent domain is the same obligation.
// expect-unresolved: LNT-047 11
// expect-count: LNT-047 WARNING 1
// expect: LNT-047 INFO 12
module top(input logic clk, output logic [7:0] q);
  logic slow = 1'b0;
  logic [7:0] d = '0;
  wire c = ~slow;
  always_ff @(posedge clk) begin slow <= ~slow; d <= d + 1'b1; end
  always_ff
    @(posedge c) q <= d;
endmodule
