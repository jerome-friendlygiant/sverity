// sverity-args: --top top
// The child drives bits 3 and 2 of its four-bit port; the connection is two bits wide, so those bits are not connected and
// the parent's drive of n is the only one.
// expect-none: LNT-009
module drv(inout wire [3:0] p, input logic [3:0] o);
  assign p[3:2] = o[3:2];
endmodule
module top(input logic [3:0] o1, input logic o2, output wire [1:0] y);
  wire [1:0] n;
  drv u1(.p(n), .o(o1));
  assign n = {o2, o2};
  assign y = n;
endmodule
