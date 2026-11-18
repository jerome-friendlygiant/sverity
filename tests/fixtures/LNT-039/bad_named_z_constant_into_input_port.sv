// sverity-args: --top top
// A named constant that is all 'z wired to an input: nothing else drives that input, so it floats, as for the literal.
// expect: LNT-039 WARNING 10
// expect-none: LNT-039 INFO
module sub(input wire a, output wire y);
  assign y = a;
endmodule
module top(output wire y);
  localparam logic HIZ = 1'bz;
  sub u(.a(HIZ), .y(y));
endmodule
