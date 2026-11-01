// sverity-args: --top top
// a register reset to X steers a condition
// expect: LNT-066 WARNING
module top(input logic clk, rst_n, a, b, output logic y);
  logic st;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= 1'bx; else st <= a;
  always_comb if (st) y = a; else y = b;
endmodule
