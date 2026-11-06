// Adapted from lowRISC/ibex rtl/ibex_fetch_fifo.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Copyright lowRISC contributors.
// Copyright 2018 ETH Zurich and University of Bologna, see also CREDITS.md.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       in_valid_i,
  input  logic [31:0] in_rdata_i,
  input  logic       out_ready_i,
  output logic       out_valid_o,
  output logic [31:0] out_rdata_o
);
  logic       valid_q;
  logic [31:0] data_q;

  assign out_valid_o = valid_q | in_valid_i;
  assign out_rdata_o = valid_q ? data_q : in_rdata_i;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      valid_q <= 1'b0;
      data_q  <= 32'b0;
    end else if (out_ready_i) begin
      valid_q <= in_valid_i;
      data_q  <= in_rdata_i;
    end
  end
endmodule
