// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic enable_i, output logic [63:0] mask_o);
  localparam logic [31:0] MASK = 1 << 40;
  assign mask_o = enable_i ? {32'b0, MASK} : 64'b0;
endmodule
