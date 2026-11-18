// sverity-args: --top top
// The base `{lane, 3'b000}` is 0, 8, 16 or 24; the select `+: 8` of the last lane reaches bit 31 of a 24-bit vector.
// expect-unresolved: LNT-006 5
module top(input logic [23:0] word_i, input logic [1:0] lane, output logic [7:0] byte_o);
  assign byte_o = word_i[{lane, 3'b000} +: 8];
endmodule
