// sverity-args: --top top
// The largest value that fits and a pattern that reads back unchanged under the literal's own signedness are clean; one past is not.
// expect: LNT-073 WARNING 7
// expect-count: LNT-073 WARNING 1
module top(output logic [3:0] fit, over, output logic signed [3:0] neg_fit, neg_edge);
  assign fit = 15;                  // fits 4 bits
  assign over = 16;                 // 5 bits needed: stored as 0
  assign neg_fit = -8;              // fits a signed 4-bit target
  assign neg_edge = -2;             // same bit pattern read back as -2
endmodule
