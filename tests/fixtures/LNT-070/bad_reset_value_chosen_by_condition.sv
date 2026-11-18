// sverity-args: --top top
// Both reset values are constants, but x picks between them: the asynchronous reset has no one value.
// expect: LNT-070 WARNING 7
module top(input logic clk, rst_n, x, d, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      if (x) q <= 1'b0;
      else q <= 1'b1;
    end
    else q <= d;
endmodule
