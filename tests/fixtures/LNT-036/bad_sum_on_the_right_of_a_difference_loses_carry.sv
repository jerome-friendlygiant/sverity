// sverity-args: --top top
// A sum on the right of a difference is judged the same way: c - (a + b) shifted right uses the truncated a + b.
// expect: LNT-036 WARNING 5
module top(input logic [7:0] a, b, c, output logic [7:0] m);
  assign m = (c - (a + b)) >> 1;
endmodule
