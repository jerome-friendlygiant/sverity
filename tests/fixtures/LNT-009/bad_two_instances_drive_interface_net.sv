// sverity-args: --top top
// revert: drv u2(.b(b), .o(o2)); => 
// revert-expect-none: LNT-009
// One module body drives a net of an interface with a plain assignment and is instantiated twice on the same interface: two continuous drivers, neither can release it.
// expect: LNT-009 ERROR 7
interface bus_if; wire w; endinterface
module drv(bus_if b, input logic o); assign b.w = o; endmodule
module top(input logic o1, o2, output wire y);
  bus_if b(); drv u1(.b(b), .o(o1)); drv u2(.b(b), .o(o2)); assign y = b.w;
endmodule
