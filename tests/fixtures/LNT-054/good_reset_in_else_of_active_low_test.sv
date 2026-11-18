// sverity-args: --top top
// `if (rst_n) <data> else <reset>`: an active-low reset read plainly selects the reset in the else branch.
// expect-none: LNT-054
module top(input logic clk, rst_n, d, output logic y);
  logic q;
  always_ff @(posedge clk or negedge rst_n) if (rst_n) q <= d; else q <= 1'b0;
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
