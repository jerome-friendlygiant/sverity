// sverity-args: --top top
// The arms of a conditional are signed and the target is unsigned: a negative arm extends into a large positive value.
// expect: LNT-004 WARNING 5
module top(input logic s, input logic signed [7:0] a, b, output logic [15:0] y);
  assign y = s ? a : b;
endmodule
