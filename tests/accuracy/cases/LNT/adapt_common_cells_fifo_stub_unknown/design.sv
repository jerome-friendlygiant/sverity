// Adapted from lowRISC/common_cells src/cc_passthrough_stream_fifo.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2024 ETH Zurich and University of Bologna.
// Solderpad Hardware License, Version 0.51, see LICENSE for details.
// SPDX-License-Identifier: SHL-0.51
// Reduced and modified for the SVerity golden set.

// The storage implementation is not part of this reduced integration shell.
module fifo_storage_stub #(
  parameter int unsigned Depth = 4,
  parameter int unsigned DataWidth = 16
) (
  input  logic                 clk_i,
  input  logic                 rst_ni,
  input  logic [DataWidth-1:0] data_i,
  input  logic                 valid_i,
  input  logic                 ready_i,
  output logic [DataWidth-1:0] data_o,
  output logic                 valid_o,
  output logic                 ready_o
);
endmodule

module top #(
  parameter int unsigned Depth = 4,
  parameter int unsigned DataWidth = 16
) (
  input  logic                 clk_i,
  input  logic                 rst_ni,
  input  logic [DataWidth-1:0] data_i,
  input  logic                 valid_i,
  input  logic                 ready_i,
  output logic [DataWidth-1:0] data_o,
  output logic                 valid_o,
  output logic                 ready_o
);
  fifo_storage_stub #(.Depth(Depth), .DataWidth(DataWidth)) u_fifo (.*);
endmodule
