// sverity-args: --top top
// Two processes write overlapping bit ranges of word 1 (bits 3:0 and 4:2 meet at bits 3:2).
// expect: LNT-049 WARNING 5
module top(input logic clk, we, input logic [3:0] d, output logic [3:0] q);
  logic [7:0] m [4];
  always_ff @(posedge clk) if (we) m[1][3:0] <= d;
  always_ff @(posedge clk) if (!we) m[1][6:2] <= {d, 1'b0};
  assign q = m[0][3:0];
endmodule
