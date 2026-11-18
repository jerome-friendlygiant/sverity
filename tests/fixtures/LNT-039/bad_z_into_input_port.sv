// sverity-args: --top top
// A 'z connected to an input port: nothing else drives that input, so it floats (an expression cannot be a shared bus).
// expect: LNT-039 WARNING 10
// revert: .a('z) => .a(1'b0)
// revert-expect-none: LNT-039
module sub(input wire a, output wire y);
  assign y = a;
endmodule
module top(output wire y);
  sub u(.a('z), .y(y));
endmodule
