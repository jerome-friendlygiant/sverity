// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [7:0] count_i, output logic [7:0] count_o);
  assign count_o = (&count_i) ? count_i : count_i + 8'd1;
endmodule
