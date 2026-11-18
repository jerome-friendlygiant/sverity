// sverity-args: --top top
// The reset test is the last statement: it overrides the enable write and leaves the constant.
// expect-none: LNT-054
module top(input logic clk, rst, en, d, output logic y);
  logic q;
  always_ff @(posedge clk) begin if (en) q <= d; if (rst) q <= 1'b0; end
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
