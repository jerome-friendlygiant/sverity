// Adapted from pulp-platform/common_cells src/cc_clk_mux_glitch_free.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright (C) 2013-2022 ETH Zurich, University of Bologna
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License").
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_a,
  input  logic clk_b,
  output logic mux_clk,
  output logic state_q
);
  localparam logic SELECT_A = 1'b0;

  assign mux_clk = SELECT_A ? clk_b : clk_a;

  always_ff @(posedge mux_clk) begin
    state_q <= ~state_q;
  end
endmodule
