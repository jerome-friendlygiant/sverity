// sverity-args: --top top
// A child drives bit 0 of an interface net through its inout port, reached by a modport port defined as w[0], and the parent drives it too.
// expect: LNT-009 ERROR 9
interface bus_if; wire [1:0] w; modport m0(inout .p(w[0])); endinterface
module drv(inout wire p, input logic o); assign p = o; endmodule
module user0(bus_if.m0 b, input logic o); drv d(.p(b.p), .o(o)); endmodule
module top(input logic o1, o2, output wire [1:0] y);
  bus_if b(); user0 u1(.b(b), .o(o1));
  assign b.w[0] = o2; assign y = b.w;
endmodule
