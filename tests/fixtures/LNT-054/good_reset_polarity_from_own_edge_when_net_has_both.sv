// sverity-args: --top top
// `init` is an asynchronous reset on both edges in different procedures and has no reset name; each procedure's own edge gives the
// polarity of its test.
// expect-none: LNT-054
module top(input logic clk, init, d, output logic y, output logic p, r);
  always_ff @(posedge clk or posedge init) if (init) p <= 1'b0; else p <= d;
  always_ff @(posedge clk or negedge init) if (!init) r <= 1'b0; else r <= d;
  always_comb begin
    if (p & r) y = d; else y = 1'b0;
  end
endmodule
