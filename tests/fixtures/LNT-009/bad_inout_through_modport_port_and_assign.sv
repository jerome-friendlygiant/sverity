// sverity-args: --top top
// revert: assign b.w = o2; => 
// revert-expect-none: LNT-009
// A child drives an interface net through its inout port, reached by a modport port, and the parent drives the same net.
// expect: LNT-009 ERROR 11
interface bus_if; wire w; modport m(inout w); endinterface
module drv(inout wire p, input logic o); assign p = o; endmodule
module user(bus_if.m b, input logic o); drv d(.p(b.w), .o(o)); endmodule
module top(input logic o1, o2, output wire y);
  bus_if b(); user u1(.b(b), .o(o1));
  assign b.w = o2; assign y = b.w;
endmodule
