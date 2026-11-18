// Adapted from Ibex rtl/ibex_register_file_ff.sv
// (commit 2373e5dadcfc4f56672962fa242f6a051acc9aca).
// Copyright lowRISC contributors.
// Copyright 2018 ETH Zurich and University of Bologna, see also CREDITS.md.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

module top #(
  parameter int unsigned          DataWidth   = 32,
  parameter logic [DataWidth-1:0] WordZeroVal = '0
) (
  input  logic                 clk_i,
  input  logic                 rst_ni,

  input  logic [4:0]           raddr_a_i,
  output logic [DataWidth-1:0] rdata_a_o,

  input  logic [4:0]           raddr_b_i,
  output logic [DataWidth-1:0] rdata_b_o,

  input  logic [4:0]           waddr_a_i,
  input  logic [DataWidth-1:0] wdata_a_i,
  input  logic                 we_a_i
);

  localparam int unsigned NumWords = 32;

  logic [NumWords-1:0][DataWidth-1:0] rf_reg;
  logic [NumWords-1:1][DataWidth-1:0] rf_reg_q;
  logic [NumWords-1:1]                we_a_dec;

  always_comb begin : we_a_decoder
    for (int unsigned i = 1; i < NumWords; i++) begin
      we_a_dec[i] = (waddr_a_i == 5'(i)) ? we_a_i : 1'b0;
    end
  end

  for (genvar i = 1; i < NumWords; i++) begin : g_rf_flops
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        rf_reg_q[i] <= WordZeroVal;
      end else if (we_a_dec[i]) begin
        rf_reg_q[i] <= wdata_a_i;
      end
    end
  end

  assign rf_reg[NumWords-1:1] = rf_reg_q;
  assign rf_reg[0]            = '0;

  assign rdata_a_o = rf_reg[raddr_a_i];
  assign rdata_b_o = rf_reg[raddr_b_i];

endmodule
