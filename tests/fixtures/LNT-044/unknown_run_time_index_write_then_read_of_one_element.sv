// sverity-args: --top top
// m[k] may or may not be m[0]: whether the read of m[0] sees the previous value depends on k, and the loop writes every element afterwards.
// expect-unresolved: LNT-044 8
module top(input logic [7:0] x, input logic [1:0] k, output logic [7:0] y);
  logic [7:0] m [4];
  always_comb begin
    m[k] = x;
    y = m[0];
    for (int i = 0; i < 4; i++) m[i] = ~x;
  end
endmodule
