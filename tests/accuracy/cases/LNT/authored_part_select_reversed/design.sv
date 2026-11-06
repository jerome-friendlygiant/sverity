// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [15:0] sample_i, output logic [3:0] sample_o);
  assign sample_o = sample_i[4:7];
endmodule
