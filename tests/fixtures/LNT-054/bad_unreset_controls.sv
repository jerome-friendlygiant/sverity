// sverity-args: --top top
// expect: LNT-054 WARNING
module top(input logic clk, d, output logic y);
  logic mode;
  always_ff @(posedge clk) mode <= d;
  always_comb if (mode) y = 1'b1; else y = 1'b0;
endmodule
