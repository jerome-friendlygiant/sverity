// sverity-args: --top top
// The first procedure reads `i` before it sets it, so it sees what the second procedure left: `i` is state with two writers.
// expect: LNT-009 ERROR 7
module top(input logic [3:0] a, b, output int y, z);
  int i;
  always @* begin y = i; for (i = 0; i < 4 && a[i]; i++) ; end
  always @* begin for (i = 0; i < 4 && b[i]; i++) ; z = i; end
endmodule
