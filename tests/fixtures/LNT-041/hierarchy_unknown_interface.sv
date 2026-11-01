// sverity-args: --top top
// expect: LNT-041 WARNING 4
interface bus_if;
  logic req;
  logic ack;
endinterface
module top(input logic clk, output logic y);
  bus_if b();
  assign b.ack = clk;
  assign y = b.req;
endmodule
