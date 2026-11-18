// sverity-args: --top top
// revert: user u2(.b(b2), .o(o2)); => user u2(.b(b1), .o(o2));
// revert-expect-none: LNT-009
// Two instances of one module drive the net of the second of two identical interface instances: the first instance has no driver at all.
// expect: LNT-009 ERROR 7
interface bus_if; wire w; endinterface
module user(bus_if b, input logic o); assign b.w = o; endmodule
module top(input logic o1, o2, output wire y, z);
  bus_if b1(); bus_if b2(); user u1(.b(b2), .o(o1)); user u2(.b(b2), .o(o2));
  assign y = b1.w; assign z = b2.w;
endmodule
