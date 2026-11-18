// sverity-args: --top top
// Two instances of one module each write the variable of their own interface instance: one writer per variable.
// expect-none: LNT-009
interface bus_if; logic v; endinterface
module drv(bus_if b, input logic clk, o); always @(posedge clk) b.v <= o; endmodule
module top(input logic clk, o1, o2, output logic y1, y2);
  bus_if b1(); bus_if b2();
  drv u1(.b(b1), .clk(clk), .o(o1)); drv u2(.b(b2), .clk(clk), .o(o2));
  assign y1 = b1.v; assign y2 = b2.v;
endmodule
