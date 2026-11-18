// sverity-args: --top top
// A final block written by a macro is a final block at its use site: its force is INFO, as if written in place.
// expect: LNT-079 INFO 8
// expect-none: LNT-079 WARNING
`define FIN(s) final begin force s = 1'b0; end
module top(input logic c, output logic q);
  assign q = c;
  `FIN(q)
endmodule
