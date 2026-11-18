// expect: LNT-047 WARNING 3
module gate_clock(input logic clk, en, output wire g);
  assign g = clk & en;
endmodule
module top(input logic clk, en, d, output logic q);
  wire g;
  gate_clock u(clk,en,g);
  always @(posedge g) q <= d;
endmodule
