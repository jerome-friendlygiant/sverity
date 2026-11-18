// sverity-args: --top top
// expect: LNT-054 WARNING
// expect-unresolved: LNT-054
module child(input logic clk, clr, d, output logic y);
  logic q;
  always_ff @(posedge clk) if (clr) q <= 0; else q <= d;
  always_comb if (q) y = d; else y = 0;
endmodule
module top(input logic clk, rst_ni, en, d, output logic y);
  child u(clk, !(rst_ni & en), d, y);
endmodule
