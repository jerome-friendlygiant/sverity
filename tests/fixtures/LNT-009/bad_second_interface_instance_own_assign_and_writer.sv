// sverity-args: --top top
// The interface assigns its own net and a module drives the net of the second of two identical instances too.
// expect: LNT-009 ERROR 5
interface bus_if; wire w; assign w = 1'b1; endinterface
module user(bus_if b, input logic o); assign b.w = o; endmodule
module top(input logic o, output wire y, z);
  bus_if b1(); bus_if b2(); user u(.b(b2), .o(o));
  assign y = b1.w; assign z = b2.w;
endmodule
