// sverity-args: --top top
// A continuous assign of a named all-'z constant to a net nothing else drives, which feeds an instance input: it floats.
// expect: LNT-039 WARNING 11
// expect-none: LNT-039 INFO
module sub(input wire a, output wire y);
  assign y = a;
endmodule
module top(input logic d, output wire y);
  localparam logic HIZ = 1'bz;
  wire n;
  assign n = HIZ;
  sub u(.a(n), .y(y));
endmodule
