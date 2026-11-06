// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [7:0] request_i, output logic valid_o, output logic [2:0] index_o);
  always_comb begin
    valid_o = |request_i;
    index_o = 3'd0;
    for (int i=0;i<8;i++) begin
      if (request_i[i]) index_o = 3'(i);
    end
  end
endmodule
