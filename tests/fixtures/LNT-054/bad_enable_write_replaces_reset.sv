// sverity-args: --top top
// The enable write follows the reset test and runs while reset is asserted, so it replaces the reset value with `d`.
// expect: LNT-054 WARNING 8
module top(input logic clk, rst, en, d, output logic y);
  logic q;
  always_ff @(posedge clk) begin if (rst) q <= 1'b0; if (en) q <= d; end
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
// revert: if (en) q <= d; => if (en) q <= 1'b1;
// revert-expect-none: LNT-054
