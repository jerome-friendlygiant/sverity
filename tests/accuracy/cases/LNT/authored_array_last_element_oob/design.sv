// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [31:0] words_i, output logic [7:0] last_o);
  wire [7:0] words [0:3];
  for (genvar j=0;j<4;j++) begin : g_unpack
    assign words[j] = words_i[8*j +: 8];
  end
  assign last_o = words[4];
endmodule
