// sverity-args: --top top
// The parent can release the port net (a z arm) while the child assigns it: the overlap is not decided (Unresolved, not ERROR).
// expect: LNT-009 WARNING 10
// expect-none: LNT-009 ERROR
module sub(input logic i, output logic o);
  assign i = 1'b0;
  assign o = i;
endmodule
module top(input logic x, en, output logic y);
  sub u(.i(en ? x : 1'bz), .o(y));
endmodule
