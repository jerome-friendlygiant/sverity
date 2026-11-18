// sverity-args: --top top
// A module writes the nets of two interface ports of one type; each interface instance gets one writer.
// expect-none: LNT-009
interface bus_if; wire w; endinterface
module user(bus_if b, bus_if c, input logic o, p); assign b.w = o; assign c.w = p; endmodule
module top(input logic [3:0] o, output wire y, z);
  bus_if b1(); bus_if b2(); bus_if b3(); bus_if b4();
  user u1(.b(b1), .c(b2), .o(o[0]), .p(o[1])); user u2(.b(b3), .c(b4), .o(o[2]), .p(o[3]));
  assign y = b1.w; assign z = b4.w;
endmodule
