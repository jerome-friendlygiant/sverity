// sverity-args: --top top
// expect-none: LNT-067
module top(input logic clk, rst_n,d, output logic q);
always @(posedge clk) if (!rst_n) q <= 0; else q <= d;
endmodule
// expect-none: LNT-020
