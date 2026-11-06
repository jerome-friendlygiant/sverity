// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic signed [15:0] sample_i, input logic [2:0] gain_i,
           output logic signed [15:0] sample_o);
  assign sample_o = sample_i >>> gain_i;
endmodule
