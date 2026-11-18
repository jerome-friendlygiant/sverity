// sverity-args: --top top
// Lookalike of the dead negative arm: the selector and the label are both -1, so the second process does write word 0.
// expect: LNT-049 WARNING 5
module top(input logic clk, input logic [7:0] a, b, output logic [7:0] y);
  logic [7:0] m [2];
  always_ff @(posedge clk) m[0] <= a;
  always_ff @(posedge clk) case (-1) -1: m[0] <= b; default: ; endcase
  assign y = m[0];
endmodule
