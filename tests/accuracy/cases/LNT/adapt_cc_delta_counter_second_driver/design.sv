// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License.
// SPDX-License-Identifier: SHL-0.51
// Adapted from src/cc_delta_counter.sv. @e73baaec2ca665cd80c3c384e9258e35242b829c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       en_i,
  input  logic       clr_i,
  output logic [3:0] count_o
);
  logic [3:0] count_q;
  wire  [3:0] next_count;
  wire  [3:0] incremented = en_i ? count_q + 4'd1 : count_q;
  wire  [3:0] cleared = clr_i ? 4'd0 : incremented;

  assign next_count = incremented;
  assign next_count = cleared;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)
      count_q <= 4'd0;
    else
      count_q <= next_count;
  end

  assign count_o = count_q;
endmodule
