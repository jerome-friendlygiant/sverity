// sverity-args: --top top
// reset values are constants (a parameter is a constant)
// expect-none: LNT-070
module top #(parameter logic INIT = 1'b1) (input logic clk, rst_n, a, output logic q, r);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= INIT; else q <= a;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= q;
endmodule
