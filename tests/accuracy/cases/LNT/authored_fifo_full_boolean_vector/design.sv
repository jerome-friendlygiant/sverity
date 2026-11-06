// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [3:0] overflow_i, underflow_i, output logic [3:0] status_o);
  assign status_o = overflow_i || underflow_i;
endmodule
