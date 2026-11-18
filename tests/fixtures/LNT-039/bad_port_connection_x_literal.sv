// sverity-args: --top top
// An input port connection is a continuous assignment of the actual to the port: an X literal there is an X value.
// expect: LNT-039 WARNING 8
module sub(input logic [1:0] a, output logic [1:0] y);
  assign y = a;
endmodule
module top(output logic [1:0] y);
  sub u(.a(2'bx0), .y(y));
endmodule
