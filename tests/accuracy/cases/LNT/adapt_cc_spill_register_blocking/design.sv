// Copyright 2018 ETH Zurich and University of Bologna.
//
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License.
// SPDX-License-Identifier: SHL-0.51
// Adapted from src/cc_spill_register.sv. @e73baaec2ca665cd80c3c384e9258e35242b829c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       valid_i,
  input  logic       ready_i,
  input  logic [7:0] data_i,
  output logic       valid_o,
  output logic [7:0] data_o
);
  logic [7:0] data_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_o <= 1'b0;
      data_q = 8'h00;
    end else if (valid_i && ready_i) begin
      data_q = data_i;
      valid_o <= 1'b1;
    end
  end

  assign data_o = data_q;
endmodule
