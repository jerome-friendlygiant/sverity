// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [15:0] position_i, input logic signed [7:0] step_i,
           output logic [15:0] position_o);
  assign position_o = position_i + step_i;
endmodule
