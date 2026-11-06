// Adapted from lowRISC/ibex rtl/ibex_register_file_ff.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Copyright lowRISC contributors; Copyright 2018 ETH Zurich and University of Bologna. Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic [4:0]  raddr_a_i,
  output logic [31:0] rdata_a_o,
  input  logic [4:0]  raddr_b_i,
  output logic [31:0] rdata_b_o,
  input  logic [4:0]  waddr_i,
  input  logic [31:0] wdata_i,
  input  logic        we_i
);
  logic [31:0] rf_q [0:31];

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      for (int i = 0; i < 32; i++) rf_q[i] <= '0;
    end else if (we_i && (waddr_i != 5'd0)) begin
      rf_q[waddr_i] <= wdata_i;
    end
  end

  assign rdata_a_o = (raddr_a_i == 5'd0) ? 32'b0 : rf_q[raddr_a_i];
  assign rdata_b_o = (raddr_b_i == 5'd0) ? 32'b0 : rf_q[raddr_b_i];
endmodule
