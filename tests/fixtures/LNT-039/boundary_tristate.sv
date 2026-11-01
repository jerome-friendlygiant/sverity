// sverity-args: --top top
// expect: LNT-039 INFO 5
// expect-none: LNT-039 WARNING
module top(input logic en, a, output wire y);
  assign y = en ? a : 'z;
endmodule
