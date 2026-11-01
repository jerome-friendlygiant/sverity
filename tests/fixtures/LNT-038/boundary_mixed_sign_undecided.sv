// sverity-args: --top top
// the same mixed comparison with a value that some operand values reach: no finding
// expect-none: LNT-038
module top(input logic signed [3:0] s, output logic y0, y1);
  assign y0 = s < 4'd2;
  assign y1 = s == 4'd3;
endmodule
