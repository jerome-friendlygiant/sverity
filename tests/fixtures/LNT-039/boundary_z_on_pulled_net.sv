// sverity-args: --top top
// A tri1 net released to z is pulled high, not floating: INFO.
// expect: LNT-039 INFO 7
// expect-none: LNT-039 WARNING
module top(input logic en, d, output wire y);
  tri1 t;
  assign t = en ? d : 1'bz;
  assign y = t;
endmodule
