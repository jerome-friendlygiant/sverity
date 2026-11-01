// sverity-args: --top top
// expect: LNT-020 WARNING 6
// expect-count: LNT-020 WARNING 2
module child(input logic clk, rst_n, a, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 1'b0;
endmodule
module top(input logic clk, rst_n, a, output logic [1:0] q);
  child u0(clk, rst_n, a, q[0]);
  child u1(clk, rst_n, a, q[1]);
endmodule
