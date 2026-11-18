// sverity-args: --top top
// A word written at a constant index on one clock and read on another, unrelated clock: the read sees the word at a phase the RTL does not define.
// expect-unresolved: LNT-082 7
// expect-unresolved: LNT-082 8
module top(input logic clk_w, clk_r, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [4];
  always_ff @(posedge clk_w) mem[1] <= d;
  always_ff @(posedge clk_r) q <= mem[1];
endmodule
