// sverity-args: --top top
// `black_box = 0` turns the attribute off: the module is ordinary RTL and is analysed.
// expect-none: CST-005
(* black_box = 0 *) module pass(input logic a, output logic b);
  assign b = a;
endmodule
module top(input logic x, output logic y);
  pass u(.a(x), .b(y));
endmodule
