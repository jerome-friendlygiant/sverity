// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top (
  input  logic [1:0] size_i,
  input  logic [2:0] offset_i,
  input  logic       unaligned_ok_i,
  output logic [7:0] strobe_o,
  output logic       misaligned_o
);

  logic [7:0] base;
  logic       bad_align;

  always_comb begin
    base      = 8'h00;
    bad_align = 1'b0;
    casez ({size_i, offset_i})
      5'b00_???: base = 8'h01;
      5'b01_??0: base = 8'h03;
      5'b01_??1: bad_align = 1'b1;
      5'b10_?00: base = 8'h0f;
      5'b10_?01,
      5'b10_?10,
      5'b10_?11: bad_align = 1'b1;
      5'b11_000: base = 8'hff;
      5'b11_001,
      5'b11_010,
      5'b11_011,
      5'b11_100,
      5'b11_101,
      5'b11_110,
      5'b11_111: bad_align = 1'b1;
    endcase
  end

  assign misaligned_o = bad_align & ~unaligned_ok_i;
  assign strobe_o     = (bad_align & ~unaligned_ok_i) ? 8'h00 : (base << offset_i);

endmodule
