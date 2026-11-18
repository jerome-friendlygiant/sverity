// sverity-args: --top top
// With several asynchronous controls, a test of one that also reads x has no flip-flop either.
// expect: LNT-058 ERROR
module top(input logic clk, rst_n, set_n, x, d, output logic q);
  always @(posedge clk or negedge rst_n or negedge set_n) if (!rst_n) q <= 1'b0; else if (!set_n && x) q <= 1'b1; else q <= d;
endmodule
