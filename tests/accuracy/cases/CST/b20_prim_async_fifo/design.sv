// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_fifo_async.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project). Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_wr_i,
  input  logic       clk_rd_i,
  input  logic       rst_wr_ni,
  input  logic       rst_rd_ni,
  input  logic       wvalid_i,
  output logic       wready_o,
  input  logic [7:0] wdata_i,
  output logic       rvalid_o,
  input  logic       rready_i,
  output logic [7:0] rdata_o
);
  logic [7:0] mem_q [0:3];
  logic [2:0] wr_bin_q, rd_bin_q, wr_gray_q, rd_gray_q;
  logic [2:0] rd_gray_wr1_q, rd_gray_wr2_q;
  logic [2:0] wr_gray_rd1_q, wr_gray_rd2_q;
  logic wr_full_q, rd_empty_q;
  logic [2:0] wr_bin_next, rd_bin_next;
  logic [2:0] wr_gray_next, rd_gray_next;

  assign wr_bin_next  = wr_bin_q + (wvalid_i & wready_o);
  assign wr_gray_next = (wr_bin_next >> 1) ^ wr_bin_next;
  assign rd_bin_next  = rd_bin_q + (rvalid_o & rready_i);
  assign rd_gray_next = (rd_bin_next >> 1) ^ rd_bin_next;
  assign wready_o     = ~wr_full_q;
  assign rvalid_o     = ~rd_empty_q;
  assign rdata_o      = mem_q[rd_bin_q[1:0]];

  always_ff @(posedge clk_wr_i or negedge rst_wr_ni) begin
    if (!rst_wr_ni) begin
      wr_bin_q      <= '0;
      wr_gray_q     <= '0;
      rd_gray_wr1_q <= '0;
      rd_gray_wr2_q <= '0;
      wr_full_q     <= 1'b0;
    end else begin
      rd_gray_wr1_q <= rd_gray_q;
      rd_gray_wr2_q <= rd_gray_wr1_q;
      if (wvalid_i && wready_o) begin
        mem_q[wr_bin_q[1:0]] <= wdata_i;
        wr_bin_q  <= wr_bin_next;
        wr_gray_q <= wr_gray_next;
      end
      wr_full_q <= (wr_gray_next == {~rd_gray_wr2_q[2:1], rd_gray_wr2_q[0]});
    end
  end

  always_ff @(posedge clk_rd_i or negedge rst_rd_ni) begin
    if (!rst_rd_ni) begin
      rd_bin_q      <= '0;
      rd_gray_q     <= '0;
      wr_gray_rd1_q <= '0;
      wr_gray_rd2_q <= '0;
      rd_empty_q    <= 1'b1;
    end else begin
      wr_gray_rd1_q <= wr_gray_q;
      wr_gray_rd2_q <= wr_gray_rd1_q;
      if (rvalid_o && rready_i) begin
        rd_bin_q  <= rd_bin_next;
        rd_gray_q <= rd_gray_next;
      end
      rd_empty_q <= (rd_gray_next == wr_gray_rd2_q);
    end
  end
endmodule
