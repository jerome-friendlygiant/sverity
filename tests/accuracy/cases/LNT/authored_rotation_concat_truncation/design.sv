// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [7:0] payload_i, output logic [7:0] rotated_o);
  always_comb begin
    rotated_o = {payload_i[6:0], payload_i[7], 1'b0};
  end
endmodule
