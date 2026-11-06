// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module resized #(parameter int W=8)(input logic [W-1:0] d_i, output logic [15:0] q_o);
  if (W < 16) begin : g_extend
    assign q_o = {{(16-W){1'b0}},d_i};
  end else begin : g_slice
    assign q_o = d_i[15:0];
  end
endmodule
module top(input logic [7:0] d_i, output wire [15:0] q_o);
  resized #(.W(8)) u_resize(.d_i(d_i),.q_o(q_o));
endmodule
