// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic clk_i, output logic [31:0] state_o);
  import "DPI-C" context function int unsigned sample_state();
  always_ff @(posedge clk_i) state_o <= sample_state();
endmodule
