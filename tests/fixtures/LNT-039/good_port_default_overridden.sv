// sverity-args: --top top
// An input port default is not a value when the instance connects the port.
// expect-none: LNT-039
module sub(input logic [1:0] a = 2'bxx, output logic [1:0] y);
  assign y = a;
endmodule
module top(input logic [1:0] d, output logic [1:0] y);
  sub u(.a(d), .y(y));
endmodule
