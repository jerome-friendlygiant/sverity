// sverity-args: --top top
// Two instances of one module write bit 0 of an interface variable through a modport output port defined as w[0]: the index is constant.
// expect: LNT-009 ERROR 7
interface bus_if; logic [1:0] w; modport m0(output .p(w[0])); endinterface
module user0(bus_if.m0 b, input logic clk, o); always @(posedge clk) b.p <= o; endmodule
module top(input logic clk, o1, o2, output logic [1:0] y);
  bus_if b(); user0 u1(.b(b), .clk(clk), .o(o1)); user0 u2(.b(b), .clk(clk), .o(o2));
  assign y = b.w;
endmodule
