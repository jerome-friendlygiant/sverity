// sverity-args: --top top
// The reset loop has no iteration, so the reset branch assigns nothing.
// expect: LNT-054 WARNING 8
module top(input logic clk, rst, d, output logic y);
  logic q;
  always_ff @(posedge clk) if (rst) for (int i = 0; i < 0; i++) q <= 1'b0; else q <= d;
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
// revert: i < 0 => i < 1
// revert-expect-none: LNT-054
