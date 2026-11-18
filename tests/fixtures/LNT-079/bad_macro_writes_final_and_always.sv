// sverity-args: --top top
// A macro that writes a final block and an always block: the final's force is advisory (INFO), the always block's is design RTL (WARNING).
// expect: LNT-079 INFO 10
// expect: LNT-079 WARNING 10
// expect-count: LNT-079 WARNING 1
// expect-count: LNT-079 INFO 1
`define BOTH(s) final force s = 1'b0; always @(s) force s = 1'b1;
module top(input logic c, output logic q);
  assign q = c;
  `BOTH(q)
endmodule
