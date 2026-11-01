// sverity-args: --top top
// expect: LNT-047 WARNING 4
module top(input logic clk, en, a, output logic q);
  always @(posedge (clk & en)) q <= a;
endmodule
