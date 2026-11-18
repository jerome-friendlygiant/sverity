// sverity-args: --top top
// A force written by a macro is reported at its use (the call line), not at the macro's define line. The body is kept as evidence.
// expect: LNT-079 WARNING 8
// expect-count: LNT-079 WARNING 1
`define FORCE0(s) force s = 1'b0
module top(input logic a, output logic y);
  logic r;
  always @(a) if (a) `FORCE0(r);
  assign y = r;
endmodule
