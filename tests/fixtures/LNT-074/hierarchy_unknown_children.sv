// sverity-args: --top top
// Only the 64-bit specialisation (u2) compares an unsized literal with an operand wider than 32 bits.
// expect: LNT-074 INFO 6
// expect-count: LNT-074 INFO 1
module child #(parameter W = 16) (input logic [W-1:0] a, output logic y);
  assign y = a < 'hFFFF_FFFF;
endmodule
module top(input logic [15:0] a16, input logic [63:0] a64, output logic y0, y1, y2);
  child u0(.a(a16), .y(y0));
  child u1(.a(a16), .y(y1));
  child #(.W(64)) u2(.a(a64), .y(y2));
endmodule
