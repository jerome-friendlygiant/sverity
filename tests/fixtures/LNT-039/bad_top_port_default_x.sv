// sverity-args: --top top
// An X default of a top-level input port is the value the port takes when nothing drives it.
// expect: LNT-039 WARNING 4
module top(input logic [1:0] a = 2'bxx, output logic [1:0] y);
  assign y = a;
endmodule
