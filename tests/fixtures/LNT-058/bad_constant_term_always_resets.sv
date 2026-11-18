// sverity-args: --top top
// F is 1, so the test is true at both levels of rst_n: the reset branch runs whatever the control does.
// expect: LNT-058 ERROR
module top #(parameter bit F = 1'b1)(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n || F) q <= 1'b0; else q <= d;
endmodule
