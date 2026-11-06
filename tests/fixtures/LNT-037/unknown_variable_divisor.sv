// sverity-args: --top top --option LNT-037.dynamic_divisor=true
// Nothing in the RTL keeps the divisor away from zero.
// expect: LNT-037 WARNING 5
module top(input logic [15:0] numerator, input logic [7:0] divisor, output logic [15:0] quotient);
  assign quotient = numerator / divisor;
endmodule
