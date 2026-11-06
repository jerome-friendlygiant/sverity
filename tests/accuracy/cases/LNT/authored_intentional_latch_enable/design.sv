// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic gate_i, input logic [7:0] d_i, output logic [7:0] q_o);
  always_latch begin
    if (gate_i) q_o <= d_i;
  end
endmodule
