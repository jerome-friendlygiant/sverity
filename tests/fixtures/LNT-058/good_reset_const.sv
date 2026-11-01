// sverity-args: --top top
// expect-none: LNT-058
module top(input logic clk, rst_n, a, output logic q);
  localparam logic Z = 1'b0;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= Z;
    else q <= a;
endmodule
