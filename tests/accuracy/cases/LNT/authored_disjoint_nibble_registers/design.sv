// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic clk_i, input logic [7:0] d_i, output logic [7:0] q_o);
  always_ff @(posedge clk_i) q_o[3:0] <= d_i[3:0];
  always_ff @(posedge clk_i) q_o[7:4] <= d_i[7:4];
endmodule
