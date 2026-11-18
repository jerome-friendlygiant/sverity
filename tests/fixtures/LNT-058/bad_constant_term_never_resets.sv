// sverity-args: --top top
// EN is 0, so the test is false at both levels of rst_n: the reset branch never runs.
// expect: LNT-058 ERROR
module top #(parameter bit EN = 1'b0)(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n && EN) q <= 1'b0; else q <= d;
endmodule
