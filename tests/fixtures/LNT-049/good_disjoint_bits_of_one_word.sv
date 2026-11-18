// sverity-args: --top top
// One clocked process per bit of word 1 and another for its upper half, the idiom that builds a word bit by bit: the bits differ.
// expect-none: LNT-049 WARNING
module top(input logic clk, we, input logic [3:0] d, output logic [3:0] q);
  logic [7:0] m [4];
  for (genvar i = 0; i < 4; i++) begin : gen_bit
    always_ff @(posedge clk) if (we) m[1][i] <= d[i];
  end
  always_ff @(posedge clk) if (!we) m[1][7:4] <= d;
  assign q = m[1][3:0];
endmodule
