// sverity-args: --top top
// expect-none: LNT-072
module top(input logic clk,rst_n,d, output logic q,r);
always @(posedge clk or negedge rst_n) if (!rst_n) q <= 0; else q <= d;
always @(posedge clk) r <= rst_n;
endmodule
