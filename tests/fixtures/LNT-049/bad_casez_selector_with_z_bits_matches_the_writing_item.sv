// sverity-args: --top top
// The wildcard selector matches the item that writes, so both processes write word 0.
// expect: LNT-049 WARNING 5
module top(input logic clk, input logic [7:0] a, b, output logic [7:0] y);
  logic [7:0] m [2];
  always @(posedge clk) m[0] <= a;
  always @(posedge clk) casez (2'b1z) 2'b00: ; 2'b11: m[0] <= b; default: ; endcase
  assign y = m[0];
endmodule
