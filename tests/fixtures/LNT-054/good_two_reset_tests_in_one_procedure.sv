// sverity-args: --top top
// `rst` and `sw_rst` are both resets of the procedure: `p` is cleared by `rst` and the `sw_rst` write stores the same constant.
// expect-none: LNT-054
module top(input logic clk, rst, sw_rst, d, output logic y);
  logic p;
  always_ff @(posedge clk) begin
    if (rst) p <= 1'b0; else p <= d;
    if (sw_rst) p <= 1'b0;
  end
  always_comb begin
    if (p) y = d; else y = 1'b0;
  end
endmodule
