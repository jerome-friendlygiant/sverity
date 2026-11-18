// sverity-args: --top top
// revert: assign m = o2; => 
// revert-expect-none: LNT-009
// The child drives both bits of {n, m} through its inout port and the parent also drives m: the connection maps port bit 0 to m.
// expect: LNT-009 ERROR 12
module drv(inout wire [1:0] p, input logic [1:0] o);
  assign p = o;
endmodule
module top(input logic [1:0] o1, input logic o2, output wire a, b);
  wire n, m;
  drv u1(.p({n, m}), .o(o1));
  assign m = o2;
  assign a = n;
  assign b = m;
endmodule
