// Adapted from lowRISC/common_cells src/cc_clk_int_div_static.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright (C) 2023 ETH Zurich, University of Bologna Copyright and related
// rights are licensed under the Solderpad Hardware License, Version 0.51 (the
// "License"); you may not use this file except in compliance with the License.
// You may obtain a copy of the License at http://solderpad.org/licenses/SHL-0.51.
// Reduced and modified for the SVerity golden set.

module top (
  input logic clk_i, input logic rst_ni, input logic enable_i, output logic clk_o
);
  logic en_meta_q, en_sync_q, en_latched_q;
  logic [1:0] div_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      en_meta_q <= 1'b0; en_sync_q <= 1'b0; div_q <= 2'b0;
    end else begin
      en_meta_q <= enable_i;
      en_sync_q <= en_meta_q;
      div_q <= div_q + 2'd1;
    end
  end
  always_ff @(negedge clk_i or negedge rst_ni)
    if (!rst_ni) en_latched_q <= 1'b0;
    else en_latched_q <= en_sync_q;

  assign clk_o = div_q[1] & en_latched_q;
endmodule
