// sverity-args: --top top
// A net declaration assignment of 'z is the net's only driver: the instance input it feeds floats.
// expect: LNT-039 WARNING 9
// expect-none: LNT-039 INFO
module sub(input wire a, output wire y);
  assign y = a;
endmodule
module top(output wire y);
  wire n = 1'bz;
  sub u(.a(n), .y(y));
endmodule
