// Adapted from OpenTitan hw/ip/prim_generic/rtl/prim_ram_1r1w.sv
// (commit 92f3d03b0dcf6d5c1933a65d1497747f67d19f4c).
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

module top #(
  parameter int Width = 32,
  parameter int Depth = 128,
  localparam int Aw   = $clog2(Depth)
) (
  input  logic             clk_i,

  input  logic             a_req_i,
  input  logic [Aw-1:0]    a_addr_i,
  input  logic [Width-1:0] a_wdata_i,
  input  logic [Width-1:0] a_wmask_i,

  input  logic             b_req_i,
  input  logic [Aw-1:0]    b_addr_i,
  output logic [Width-1:0] b_rdata_o
);

  logic [Width-1:0] mem [Depth];

  logic [Width-1:0] wdata;
  assign wdata = (a_wdata_i & a_wmask_i) | (mem[a_addr_i] & ~a_wmask_i);

  always_ff @(posedge clk_i) begin
    if (a_req_i) begin
      mem[a_addr_i] <= wdata;
    end
  end

  always_ff @(posedge clk_i) begin
    if (b_req_i) begin
      b_rdata_o <= mem[b_addr_i];
    end
  end

endmodule
