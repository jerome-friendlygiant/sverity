// sverity-args: --top top
// The interface assigns its net inside a generate block; slang analyses that body for the first instance only, so the second instance's own assignment and the module's write are two drivers.
// expect: LNT-009 ERROR 5
interface bus_if; if (1) begin : G wire w; assign w = 1'b0; end endinterface
module user(bus_if b, input logic o); assign b.G.w = o; endmodule
module top(input logic o, output wire y, z);
  bus_if b1(); bus_if b2(); user u(.b(b2), .o(o));
  assign y = b1.G.w; assign z = b2.G.w;
endmodule
