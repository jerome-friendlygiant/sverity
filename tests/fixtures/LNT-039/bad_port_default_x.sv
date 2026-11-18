// sverity-args: --top top
// An X default of an input port is the value an instance takes when it does not connect the port.
// expect: LNT-039 WARNING 4
module sub(input logic [1:0] a = 2'bxx, output logic [1:0] y);
  assign y = a;
endmodule
module top(output logic [1:0] y);
  sub u(.y(y));
endmodule
