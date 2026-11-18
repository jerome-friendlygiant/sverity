// sverity-args: --top top
// A parameter of an interface reached through an interface port has the same value: its X bits reach the assigned value.
// expect: LNT-039 WARNING 6
interface bus_if; localparam logic [1:0] PX = 2'bx1; endinterface
module sub(bus_if b, output logic [1:0] y);
  assign y = b.PX;
endmodule
module top(output logic [1:0] y);
  bus_if b();
  sub u(.b(b), .y(y));
endmodule
