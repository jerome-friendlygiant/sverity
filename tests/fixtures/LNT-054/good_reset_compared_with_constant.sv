// sverity-args: --top top
// A reset test may compare the reset with a constant; the constant gives the polarity.
// expect-none: LNT-054
module top(input logic clk, rst_n, rst, d, output logic y);
  logic a, b;
  always_ff @(posedge clk) if (rst_n == 1'b0) a <= 1'b0; else a <= d;
  always_ff @(posedge clk) if (rst == 1'b1) b <= 1'b0; else b <= d;
  always_comb begin
    if (a & b) y = d; else y = 1'b0;
  end
endmodule
