// sverity-args: --top top
// `sw_rst` is a reset too, but its branch loads data after `rst` cleared `p`: while both are asserted the value is not the reset value.
// expect: LNT-054 WARNING 11
module top(input logic clk, rst, sw_rst, d, output logic y);
  logic p;
  always_ff @(posedge clk) begin
    if (rst) p <= 1'b0; else p <= d;
    if (sw_rst) p <= d;
  end
  always_comb begin
    if (p) y = d; else y = 1'b0;
  end
endmodule
