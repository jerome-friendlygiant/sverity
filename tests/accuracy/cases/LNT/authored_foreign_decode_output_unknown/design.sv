// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [7:0] opcode_i, output logic [31:0] control_o);
  import "DPI-C" function void decode(input byte unsigned opcode, output int unsigned control);
  always_comb begin
    decode(opcode_i,control_o);
  end
endmodule
