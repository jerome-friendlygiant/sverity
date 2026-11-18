// sverity-args: --top top
// Evaluated constants lose value just as direct positive literals do.
// expect: LNT-073 WARNING 8
// expect: LNT-073 WARNING 9
// expect-count: LNT-073 WARNING 2
module top(output logic [3:0] x, output logic signed [3:0] y);
  localparam int P = 16 + 16;
  assign x = P;
  assign y = -17;
endmodule
