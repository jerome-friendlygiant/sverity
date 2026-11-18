// sverity-args: --top top
// Two clocked processes write words 0 and 1, each through a loop that only walks the packed part of its word: no word has two writers.
// expect-none: LNT-049
module top(input logic clk, ld0, ld1, input logic [3:0][7:0] d, output logic [15:0] q);
  logic [31:0] k [2];
  always_ff @(posedge clk) if (ld0) for (int j = 0; j < 4; j++) k[0][8*j+:8] <= d[j];
  always_ff @(posedge clk) if (ld1) for (int j = 0; j < 4; j++) k[1][8*j+:8] <= ~d[j];
  assign q = k[0][15:0] ^ k[1][15:0];
endmodule
