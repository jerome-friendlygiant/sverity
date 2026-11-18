// sverity-args: --top top
// `init` is an asynchronous reset on `posedge` for `p` and on `negedge` for `r`; the third block tests it without an edge of its own, so its
// polarity is not known and `q` may or may not be reset.
// expect-unresolved: LNT-054 11
module top(input logic clk, init, d, output logic y, p, r);
  logic q;
  always_ff @(posedge clk or posedge init) if (init) p <= 1'b0; else p <= d;
  always_ff @(posedge clk or negedge init) if (!init) r <= 1'b0; else r <= d;
  always_ff @(posedge clk) if (init) q <= d; else q <= 1'b0;
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
