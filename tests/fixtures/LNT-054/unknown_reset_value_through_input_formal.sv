// sverity-args: --top top
// expect: LNT-054 WARNING
// expect-unresolved: LNT-054
module top(input logic clk, rst, d, output logic y);
  logic q;
  task automatic load(input logic v); q <= v; endtask
  always_ff @(posedge clk) if (rst) load(1'b0); else q <= d;
  always_comb if (q) y = d; else y = 0;
endmodule
