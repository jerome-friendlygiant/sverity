// Copyright 2018 ETH Zurich and University of Bologna.
// Solderpad Hardware License, Version 0.51, see LICENSE for details.
// SPDX-License-Identifier: SHL-0.51
// Adapted from src/cc_lzc.sv. @e73baaec2ca665cd80c3c384e9258e35242b829c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic [7:0] in_i,
  output logic [3:0] status_o,
  output logic       empty_o
);
  logic [3:0] count_full;
  logic [4:0] status_full;

  always_comb begin
    count_full = 4'd8;
    empty_o = 1'b1;
    for (int i = 7; i >= 0; i--) begin
      if (in_i[i] && empty_o) begin
        count_full = 4'(7 - i);
        empty_o = 1'b0;
      end
    end
    status_full = {empty_o, count_full};
    status_o = status_full;
  end
endmodule
