// sverity-args: --top top
// expect: LNT-068 INFO 5
module top(input logic clk,rst_n,d, output logic q,r);
always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 0; else q <= d;
always_ff @(posedge clk) r <= ~d;
endmodule
