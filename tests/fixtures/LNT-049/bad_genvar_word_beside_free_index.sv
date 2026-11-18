// sverity-args: --top top
// Each generate iteration writes its own word, and a further process writes at a free address, which may meet any of them.
// expect: LNT-049 WARNING 6
// expect-count: LNT-049 WARNING 1
module top(input logic clk, we, input logic [1:0] a, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] m [4];
  for (genvar k = 0; k < 4; k++) begin : gen_word
    always_ff @(posedge clk) if (we) m[k] <= d;
  end
  always_ff @(posedge clk) if (!we) m[a] <= ~d;
  assign q = m[0];
endmodule
