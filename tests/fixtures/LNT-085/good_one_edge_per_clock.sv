// sverity-args: --top top
// Each clock is used on one edge only.
// expect-none: LNT-085
module top(input logic clk, clk2, d, output logic qa, qb);
  always_ff @(posedge clk) qa <= d;
  always_ff @(negedge clk2) qb <= d;
endmodule
