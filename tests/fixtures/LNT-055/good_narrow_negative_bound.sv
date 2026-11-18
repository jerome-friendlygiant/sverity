// sverity-args: --top top
// The comparison zero-extends the narrow signed bound to 255.
// expect-none: LNT-055
module top(input logic [31:0] a, b, output logic y);
  assign y = (a - b) <= 8'shff;
endmodule
