// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [15:0] word_i, output logic [7:0] low_o);
  assign low_o = word_i[7:0];
endmodule
