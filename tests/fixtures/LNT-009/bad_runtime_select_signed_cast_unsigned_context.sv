// sverity-args: --top top
// $signed(i) + 3'd2 is an unsigned three-bit sum, so the signed two-bit operand is zero-extended: 0 to 3 plus 2 reaches bit 4.
// expect: LNT-009 ERROR 6
module top(input logic clk, input logic [1:0] i, input logic a, b, output logic [7:0] y);
  always @(posedge clk) y[$signed(i) + 3'd2] <= a;
  always @(posedge clk) y[4] <= b;
endmodule
