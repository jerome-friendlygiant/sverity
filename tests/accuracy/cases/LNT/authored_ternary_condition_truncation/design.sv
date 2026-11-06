// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [3:0] pending_i, input logic [7:0] fast_i, slow_i,
           output logic [7:0] data_o);
  logic select;
  assign select = pending_i;
  assign data_o = select ? fast_i : slow_i;
endmodule
