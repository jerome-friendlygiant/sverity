// sverity-args: --top top
// Inside `rst || load` the nested `if (rst)` stores 0 while `rst` is asserted and the `else` runs only without it: the reset leaves
// a constant, so `q` is not an unreset register that steers control flow.
// expect-none: LNT-054
// expect-none: LNT-067
module top(input logic clk, rst, load, d, din, output logic y);
  logic q;
  always_ff @(posedge clk) if (rst || load) begin if (rst) q <= 1'b0; else q <= din; end else q <= d;
  always_comb if (q) y = d; else y = 1'b0;
endmodule
