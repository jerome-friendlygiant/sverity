// sverity-args: --top top
// The element index is the loop variable plus a growing offset: slang reports the whole array driven, but only the even
// elements are written.
// expect: LNT-041 WARNING 6
module top(input logic c, output logic y);
  logic a [8];
  always_comb begin
    automatic int off = 0;
    for (int k = 0; k < 4; k++) begin a[k + off] = c; off = off + 1; end
  end
  assign y = a[1];
endmodule
