// sverity-args: --top top
// A sized 'z narrower than its net is zero-extended: w2 = 1'bz drives 2'b0z, so the upper bit is a 0 driver, not a
// tri-state. The unbased unsized 'z fills every bit and stays the tri-state INFO.
// expect: LNT-039 WARNING 7
// expect: LNT-039 INFO 8
module top(output wire [1:0] w2, output wire [1:0] w3);
  assign w2 = 1'bz;
  assign w3 = 'z;
endmodule
