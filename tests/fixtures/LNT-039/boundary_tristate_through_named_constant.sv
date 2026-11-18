// sverity-args: --top top
// An X-free 'z named constant driving a net is a tri-state, as the 'z literal is (INFO). The same constant assigned to a variable is not.
// expect: LNT-039 INFO 8
// expect: LNT-039 WARNING 9
module top(input logic en, a, output wire y);
  localparam logic HIZ = 1'bz;
  logic v;
  assign y = en ? a : HIZ;
  always_comb v = en ? a : HIZ;
endmodule
