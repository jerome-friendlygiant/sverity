// sverity-args: --top top
// The tri-state INFO needs every bit that reaches the net to be z: 2'bz0 narrowed to one bit drives 0, and 2'bz1 on a
// 2-bit net drives a 1 bit; both can contend with another driver. A z-only value, narrowed or not, stays INFO.
// expect: LNT-039 WARNING 10
// expect: LNT-039 WARNING 11
// expect: LNT-039 INFO 12
// expect: LNT-039 INFO 13
// expect-count: LNT-039 WARNING 2
module top(output wire n1, output wire [1:0] n2, output wire n3, output wire [7:0] n4);
  assign n1 = 2'bz0;
  assign n2 = 2'bz1;
  assign n3 = 2'bzz;
  assign n4 = 16'hzzzz;
endmodule
