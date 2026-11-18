// sverity-args: --top top
// The named all-'z constant is the idle arm of two exclusive drivers of one net: a bus, so INFO.
// expect: LNT-039 INFO 9
// expect: LNT-039 INFO 10
// expect-none: LNT-039 WARNING
module top(input logic ea, eb, a, b, output logic y);
  localparam logic HIZ = 1'bz;
  wire bus;
  assign bus = ea ? a : HIZ;
  assign bus = eb ? b : HIZ;
  assign y = bus;
endmodule
