// sverity-args: --top top
// bits 7:4 are driven by both drivers whatever the enables are; a partly-z arm is not a release
// expect: LNT-009 WARNING
module top(input logic en, input logic [7:0] a, b, output wire [7:0] w);
  assign w = en ? a : 8'b0000_zzzz;
  assign w = !en ? b : 8'b0000_zzzz;
endmodule
