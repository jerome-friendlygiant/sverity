// sverity-args: --top top
// An arm that is a named all-'z constant, on an input nothing else drives: the input floats when the enable is low.
// expect: LNT-039 WARNING 10
// expect-none: LNT-039 INFO
module sub(input wire a, output wire y);
  assign y = a;
endmodule
module top(input logic en, d, output wire y);
  localparam logic HIZ = 1'bz;
  sub u(.a(en ? d : HIZ), .y(y));
endmodule
