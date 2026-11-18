// sverity-args: --top top
// Both procedures set `i` in the loop header before they read it, also after the loop: the value never leaves the procedure's
// own activation, so `i` is scratch space and the two writers are no drivers.
// expect-none: LNT-009
module top(input logic [3:0] a, b, output int y, z);
  int i;
  always @* begin for (i = 0; i < 4 && a[i]; i++) ; y = i; end
  always @* begin for (i = 0; i < 4 && b[i]; i++) ; z = i; end
endmodule
