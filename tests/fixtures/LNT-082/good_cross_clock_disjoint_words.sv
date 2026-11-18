// sverity-args: --top top
// The two clocks are unrelated, but the word the reader selects is not the word the writer writes: two registers, no shared word.
// expect-none: LNT-082
module top(input logic clk_w, clk_r, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [4];
  always_ff @(posedge clk_w) mem[0] <= d;
  always_ff @(posedge clk_r) q <= mem[1];
endmodule
