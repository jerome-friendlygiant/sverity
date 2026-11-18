// sverity-args: --top top
// Three lanes only (the case excludes lane 3), and a 32-bit vector for the full range of the base.
// expect-none: LNT-006
module top(input logic [31:0] word_i, input logic [1:0] lane, output logic [7:0] byte_o, output logic [7:0] byte2_o);
  assign byte_o = word_i[{lane, 3'b000} +: 8];
  logic [23:0] narrow;
  assign narrow = word_i[23:0];
  always_comb begin
    byte2_o = '0;
    if (lane < 2'd3) byte2_o = narrow[{lane, 3'b000} +: 8];
  end
endmodule
