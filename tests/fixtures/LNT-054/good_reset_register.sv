// sverity-args: --top top
// expect-none: LNT-054
module top(input logic clk, rst_n, d, output logic y);
  logic mode;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) mode <= 1'b0; else mode <= d;
  always_comb if (mode) y = 1'b1; else y = 1'b0;
endmodule
