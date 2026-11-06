// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic clk_i, reset_i, enable_i, output logic [7:0] count_o);
  always_ff @(posedge clk_i) begin
    if (reset_i) count_o <= 8'd0;
    else if (enable_i) count_o <= count_o + 8'd1;
  end
endmodule
