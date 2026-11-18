// sverity-args: --top top
// The enable can leave `n` at z, and nothing else drives `n`: the instance input it feeds floats.
// expect: LNT-039 WARNING 9
module sub(input logic i, output logic o);
  assign o = i;
endmodule
module top(input logic en, d, output logic o);
  wire n;
  assign n = en ? d : 1'bz;
  sub u(.i(n), .o(o));
endmodule
