// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [3:0] command_i, output logic [1:0] action_o);
  always_comb begin
    casez(command_i)
      4'b00??: action_o = 2'd0;
      4'b01??: action_o = 2'd1;
      4'b10??: action_o = 2'd2;
      default: action_o = 2'd3;
    endcase
  end
endmodule
