// sverity-args: --top top
// Both operands have their top bit fixed to one, so the byte sum is at least 256 whatever the inputs are: the carry is always lost.
// expect: LNT-036 WARNING 9
// expect-count: LNT-036 WARNING 1
module top(input logic [7:0] a, b, output logic [7:0] total);
  logic [7:0] hi_a, hi_b;
  assign hi_a = {1'b1, a[6:0]};
  assign hi_b = {1'b1, b[6:0]};
  assign total = hi_a + hi_b;
endmodule
