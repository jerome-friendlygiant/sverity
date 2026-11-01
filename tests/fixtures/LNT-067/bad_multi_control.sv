// sverity-args: --top top
// a block with two asynchronous controls: r is assigned in neither reset branch
// expect: LNT-067 WARNING
module top(input logic clk, rst_n, set_n, d, output logic q, r);
  always_ff @(posedge clk or negedge rst_n or negedge set_n)
    if (!rst_n) q <= 1'b0; else if (!set_n) q <= 1'b1; else begin q <= d; r <= d; end
endmodule
