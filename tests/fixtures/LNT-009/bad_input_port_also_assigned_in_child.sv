// sverity-args: --top top
// The parent drives the input port and the child assigns the same net: two continuous drivers, neither able to release it.
// expect: LNT-009 ERROR 9
module sub(input logic i, output logic o);
  assign i = 1'b0;
  assign o = i;
endmodule
module top(input logic x, output logic y);
  sub u(.i(x), .o(y));
endmodule
