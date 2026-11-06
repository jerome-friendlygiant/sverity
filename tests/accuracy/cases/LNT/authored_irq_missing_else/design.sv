// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top(input logic [3:0] irq_i, output logic valid_o, output logic [1:0] index_o);
  always_comb begin
    index_o = 2'd0;
    if (irq_i != 4'b0) begin
      valid_o = 1'b1;
      if (irq_i[3]) index_o = 2'd3;
      else if (irq_i[2]) index_o = 2'd2;
      else if (irq_i[1]) index_o = 2'd1;
    end
  end
endmodule
