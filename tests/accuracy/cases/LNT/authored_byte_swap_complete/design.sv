// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [31:0] word_i, output logic [31:0] word_o);
  for (genvar b=0;b<4;b++) begin : g_swap
    assign word_o[8*b +: 8] = word_i[8*(3-b) +: 8];
  end
endmodule
