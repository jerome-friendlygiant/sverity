// sverity-args: --top top
// Two instances of one module drive bit 0 of an interface net through a modport output port defined as w[0].
// expect: LNT-009 ERROR 5
interface bus_if; wire [1:0] w; modport m0(output .p(w[0])); endinterface
module user0(bus_if.m0 b, input logic o); assign b.p = o; endmodule
module top(input logic o1, o2, output wire [1:0] y);
  bus_if b(); user0 u1(.b(b), .o(o1)); user0 u2(.b(b), .o(o2));
  assign y = b.w;
endmodule
