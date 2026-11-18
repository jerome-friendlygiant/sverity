// sverity-args: --top top
// expect-none: LNT-063
module top(input logic clk,d, output logic q);
initial q <= 0;
always @(posedge clk) q <= d;
endmodule
