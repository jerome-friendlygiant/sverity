// Adapted from verilog-axis rtl/axis_frame_len.v
// (commit 48ff7a7e2ef782cf778d47910cf85835c64b1bce).
// Copyright (c) 2019 Alex Forencich
// Licensed under the MIT License.
// SPDX-License-Identifier: MIT

module top #(
  parameter DATA_WIDTH = 64,
  parameter KEEP_WIDTH = ((DATA_WIDTH+7)/8),
  parameter LEN_WIDTH  = 16
) (
  input  logic                  clk,
  input  logic                  rst,
  input  logic [KEEP_WIDTH-1:0] monitor_axis_tkeep,
  input  logic                  monitor_axis_tvalid,
  input  logic                  monitor_axis_tready,
  input  logic                  monitor_axis_tlast,
  output logic [LEN_WIDTH-1:0]  frame_len,
  output logic                  frame_len_valid
);

  logic [LEN_WIDTH-1:0] frame_len_reg = 0, frame_len_next;
  logic frame_len_valid_reg = 1'b0, frame_len_valid_next;

  assign frame_len = frame_len_reg;
  assign frame_len_valid = frame_len_valid_reg;

  integer i, bit_cnt;

  always_comb begin
    frame_len_next = frame_len_reg;
    frame_len_valid_next = 1'b0;
    bit_cnt = 0;

    if (frame_len_valid_reg) begin
      frame_len_next = '0;
    end

    if (monitor_axis_tready && monitor_axis_tvalid) begin
      if (monitor_axis_tlast) begin
        frame_len_valid_next = 1'b1;
      end
      for (i = 0; i <= KEEP_WIDTH; i = i + 1) begin
        if (monitor_axis_tkeep == ({KEEP_WIDTH{1'b1}}) >> (KEEP_WIDTH-i)) bit_cnt = i;
      end
      frame_len_next = frame_len_next + LEN_WIDTH'(bit_cnt);
    end
  end

  always_ff @(posedge clk) begin
    frame_len_reg <= frame_len_next;
    frame_len_valid_reg <= frame_len_valid_next;

    if (rst) begin
      frame_len_reg <= '0;
      frame_len_valid_reg <= 1'b0;
    end
  end

endmodule
