// sverity-args: --top top
// the child body is shared by two instances with the same width: one finding, count 2
// expect: LNT-038 WARNING 6
// expect-count: LNT-038 WARNING 2
module child #(parameter int W = 3) (input logic [W-1:0] x, output logic y);
  assign y = x == 9;
endmodule
module top(input logic [2:0] a, b, output logic y0, y1);
  child #(.W(3)) u0(a, y0);
  child #(.W(3)) u1(b, y1);
endmodule
