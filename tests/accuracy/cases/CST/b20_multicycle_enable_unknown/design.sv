// Adapted from pulp-platform/common_cells src/cc_shift_register_gated.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna. Copyright and related rights are licensed under the Solderpad Hardware License, Version 0.51 (the "License").
// Reduced and modified for the SVerity golden set. SPDX-License-Identifier: SHL-0.51

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       launch_i,
  input  logic       enable_i,
  input  logic [7:0] data_i,
  output logic [7:0] data_o,
  output logic       valid_o
);
  logic [7:0] src_q, dst_q;
  logic       valid_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      src_q   <= '0;
      dst_q   <= '0;
      valid_q <= 1'b0;
    end else begin
      if (launch_i) begin
        src_q   <= data_i;
        valid_q <= 1'b1;
      end
      if (enable_i && valid_q) begin
        dst_q   <= src_q;
        valid_q <= 1'b0;
      end
    end
  end

  assign data_o  = dst_q;
  assign valid_o = valid_q;
endmodule
