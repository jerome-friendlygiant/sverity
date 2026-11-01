// sverity-args: --top top
// expect: LNT-052 INFO
module top(input logic clk, a, b, c, output logic q);
  always_ff @(posedge clk) begin
    if (a) q <= 1'b0;
    else if (b) q <= 1'b1;
    else if (c) q <= ~q;
  end
endmodule
