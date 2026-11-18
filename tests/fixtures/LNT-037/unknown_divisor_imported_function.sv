// sverity-args: --top top
// The divisor is the result of an imported function: its body is not readable, so it counts as design state and nothing keeps it away from zero.
// expect-unresolved: LNT-037 6
module top(input logic [7:0] a, output logic [7:0] y);
  import "DPI-C" function byte unsigned den();
  assign y = a / den();
endmodule
