// sverity-args: --top top
// The branch of `rst || load` also runs when only `load` holds. `p` stores a constant while `rst` is asserted (`rst ? 1'b0 : din`), so it is
// reset; `q` stores `din` there, which is not a reset value, and the message says so.
// expect-count: LNT-054 WARNING 1
// expect: LNT-054 WARNING 11
module top(input logic clk, rst, load, d, din, output logic y, z);
  logic p, q;
  always_ff @(posedge clk) if (rst || load) p <= rst ? 1'b0 : din; else p <= d;
  always_ff @(posedge clk) if (rst || load) q <= din; else q <= d;
  always_comb if (p) y = d; else y = 1'b0;
  always_comb if (q) z = d; else z = 1'b0;
endmodule
