// sverity-args: --top top
// the reset input feeds the logic that generates the clock of a register
// expect: LNT-072 WARNING
module top(input logic clk, rst_n, d, output logic q);
  logic gclk;
  assign gclk = clk & rst_n;
  always_ff @(posedge gclk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
