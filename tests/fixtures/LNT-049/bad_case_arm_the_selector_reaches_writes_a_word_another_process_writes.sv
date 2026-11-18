// sverity-args: --top top
// Lookalike of the dead case arm: the selector is 1 and the arm lists 1, so the second process does write word 0.
// expect: LNT-049 WARNING 6
module top(input logic clk, input logic [7:0] a, b, output logic [7:0] y);
  localparam int SEL = 1;
  logic [7:0] m [2];
  always_ff @(posedge clk) m[0] <= a;
  always_ff @(posedge clk) case (SEL) 1: m[0] <= b; default: ; endcase
  assign y = m[0];
endmodule
