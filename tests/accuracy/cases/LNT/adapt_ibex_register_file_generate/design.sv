// Adapted from lowRISC/ibex rtl/ibex_register_file_ff.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Copyright lowRISC contributors.
// Copyright 2018 ETH Zurich and University of Bologna, see also CREDITS.md.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top #(
  parameter bit RV32E = 0,
  parameter int unsigned DataWidth = 32,
  localparam int unsigned AddrWidth = RV32E ? 4 : 5,
  localparam int unsigned NumWords = 2 ** AddrWidth
) (
  input  logic                  clk_i,
  input  logic                  rst_ni,
  input  logic [4:0]            raddr_i,
  output logic [DataWidth-1:0]  rdata_o,
  input  logic [4:0]            waddr_i,
  input  logic [DataWidth-1:0]  wdata_i,
  input  logic                  we_i
);
  logic [DataWidth-1:0] rf_reg [NumWords];
  logic [NumWords-1:0] we_dec;

  always_comb begin
    for (int unsigned i = 0; i < NumWords; i++)
      we_dec[i] = (waddr_i == 5'(i)) ? we_i : 1'b0;
  end

  for (genvar i = 1; i < NumWords; i++) begin : gen_rf_flops
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) rf_reg[i] <= '0;
      else if (we_dec[i]) rf_reg[i] <= wdata_i;
    end
  end

  always_comb begin
    rdata_o = '0;
    if (raddr_i != '0 && (!RV32E || !raddr_i[4]))
      rdata_o = rf_reg[raddr_i[AddrWidth-1:0]];
  end
endmodule
