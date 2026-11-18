// sverity-args: --top top
// Two instances of one module write the variable of the second of two identical interface instances.
// expect: LNT-009 ERROR 7
interface bus_if; logic w; endinterface
module user(bus_if b, input logic clk, o); always @(posedge clk) b.w <= o; endmodule
module top(input logic clk, o1, o2, output logic y, z);
  bus_if b1(); bus_if b2(); user u1(.b(b2), .clk(clk), .o(o1)); user u2(.b(b2), .clk(clk), .o(o2));
  assign y = b1.w; assign z = b2.w;
endmodule
