// sverity-args: --top top
// expect-none: LNT-058
module top(input logic clk, rst_n, set_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n or negedge set_n) if (!rst_n) q <= 1'b0; else if (!set_n) q <= 1'b1; else q <= d;
endmodule
