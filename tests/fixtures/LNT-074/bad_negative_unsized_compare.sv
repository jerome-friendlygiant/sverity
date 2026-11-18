// sverity-args: --top top
// Unary minus preserves the operand's unsized-literal origin.
// expect: LNT-074 INFO 6
// expect-count: LNT-074 INFO 1
module top(input logic [63:0] a, output logic y);
  assign y = a < -1;
endmodule
