// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [1:0] request_i, output wire result_o);
  for (genvar lane=0; lane<2; lane++) begin : g_lane
    assign result_o = request_i[lane];
  end
endmodule
