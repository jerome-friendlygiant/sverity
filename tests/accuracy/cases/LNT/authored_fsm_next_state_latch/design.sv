// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [1:0] state_i, input logic done_i, output logic [1:0] next_o);
  always_comb begin
    case (state_i)
      2'd0: next_o = 2'd1;
      2'd1: if (done_i) next_o = 2'd2;
      default: next_o = 2'd0;
    endcase
  end
endmodule
