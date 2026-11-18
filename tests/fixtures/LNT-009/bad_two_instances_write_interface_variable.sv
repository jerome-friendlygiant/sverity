// sverity-args: --top top
// revert: drv u2(.b(b), .clk(clk), .o(o2)); => 
// revert-expect-none: LNT-009
// One module body writes a variable of an interface through its port, and it is instantiated twice on the same interface:
// the writers are the two instances, not the one definition.
// expect: LNT-009 ERROR 10
interface bus_if; logic v; endinterface
module drv(bus_if b, input logic clk, o); always @(posedge clk) b.v <= o; endmodule
module top(input logic clk, o1, o2, output logic y);
  bus_if b(); drv u1(.b(b), .clk(clk), .o(o1)); drv u2(.b(b), .clk(clk), .o(o2)); assign y = b.v;
endmodule
