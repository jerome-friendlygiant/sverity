// sverity-args: --top top
// expect: LNT-058 ERROR 4
module top(input logic clk, rst, a, output logic q);
  always_ff @(posedge clk or posedge rst)
    if (!rst) q <= 1'b0;
    else q <= a;
endmodule
