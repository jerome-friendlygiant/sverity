// sverity-args: --top top
// revert: .c(b1), .x(o[2]) => .c(b3), .x(o[2])
// revert-expect-none: LNT-009
// A wrapper forwards each of two ports to a child. In the second wrapper the child on port c writes b1, which the first wrapper's child on port a also writes; slang analyses only the first wrapper's body.
// expect: LNT-009 ERROR 7
interface bus_if; wire w; endinterface
module leaf(bus_if q, input logic x); assign q.w = x; endmodule
module wrap(bus_if a, bus_if c, input logic x, y); leaf l1(.q(a), .x(x)); leaf l2(.q(c), .x(y)); endmodule
module top(input logic [3:0] o, output wire [3:0] z);
  bus_if b0(); bus_if b1(); bus_if b2(); bus_if b3(); wrap u1(.a(b1), .c(b0), .x(o[0]), .y(o[1]));
  wrap u2(.a(b2), .c(b1), .x(o[2]), .y(o[3]));
  assign z = {b0.w, b1.w, b2.w, b3.w};
endmodule
