// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [3:0] opcode_i, output logic [1:0] mode_o);
  always_comb begin
    priority casez (opcode_i)
      4'b1???: mode_o = 2'd1;
      4'b110?: mode_o = 2'd2;
      default: mode_o = 2'd0;
    endcase
  end
endmodule
