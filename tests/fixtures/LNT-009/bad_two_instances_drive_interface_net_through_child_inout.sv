// sverity-args: --top top
// revert: user u2(.b(b), .o(o2)); => 
// revert-expect-none: LNT-009
// Two instances of one module each pass an interface net to a child's inout port, and the child drives it with a plain assignment: the second instance's body is not the analysed one, but its connection is still its own driver.
// expect: LNT-009 ERROR 7
interface bus_if; wire w; endinterface
module drv(inout wire p, input logic o); assign p = o; endmodule
module user(bus_if b, input logic o); drv d(.p(b.w), .o(o)); endmodule
module top(input logic o1, o2, output wire y);
  bus_if b();
  user u1(.b(b), .o(o1)); user u2(.b(b), .o(o2)); assign y = b.w;
endmodule
