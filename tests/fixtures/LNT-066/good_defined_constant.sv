// sverity-args: --top top
// defined constants and an X that only reaches data, never a condition
// expect-none: LNT-066
module top(input logic clk, rst_n, a, b, output logic y, z);
  logic st;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= 1'b0; else st <= a;
  assign z = 1'bx;
  always_comb if (st) y = a; else y = b;
endmodule
