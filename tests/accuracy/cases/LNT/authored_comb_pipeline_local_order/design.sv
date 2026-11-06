// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [7:0] a_i,b_i,c_i, output logic [9:0] sum_o);
  logic [8:0] first;
  always_comb begin
    first = {1'b0,a_i} + {1'b0,b_i};
    sum_o = {1'b0,first} + {2'b0,c_i};
  end
endmodule
