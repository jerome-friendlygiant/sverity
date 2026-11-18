// sverity-args: --top top
// The open-drain idiom: the 1'bz arm is widened to the 32-bit ?: type (zero fill) and narrowed back to the net, so only
// its z bit reaches the net: tri-state INFO. A z arm whose filled bits reach a wider net drives 0 bits: WARNING.
// expect: LNT-039 INFO 7
// expect: LNT-039 WARNING 8
module top(input logic oe, input logic s, output wire sda, output wire [7:0] q);
  assign sda = oe ? 0 : 1'bz;
  assign q = s ? 8'hzz : 1'bz;
endmodule
