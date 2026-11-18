// sverity-args: --top top
// A table filled by a constant-bound initial loop whose values depend only on the loop counter.
// expect-none: LNT-082
module top(input logic clk, input logic [3:0] a, output logic [7:0] q);
  logic [7:0] rom [16];
  initial for (int i = 0; i < 16; i++) rom[i] = 8'(i * 3);
  always_ff @(posedge clk) q <= rom[a];
endmodule
