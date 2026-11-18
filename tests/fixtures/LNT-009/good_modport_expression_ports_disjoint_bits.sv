// sverity-args: --top top
// Two modports define their port as different bits of one interface net: one writer per bit.
// expect-none: LNT-009
interface bus_if; wire [1:0] w; modport m0(output .p(w[0])); modport m1(output .p(w[1])); endinterface
module user0(bus_if.m0 b, input logic o); assign b.p = o; endmodule
module user1(bus_if.m1 b, input logic o); assign b.p = o; endmodule
module top(input logic o1, o2, output wire [1:0] y);
  bus_if b(); user0 u1(.b(b), .o(o1)); user1 u2(.b(b), .o(o2));
  assign y = b.w;
endmodule
