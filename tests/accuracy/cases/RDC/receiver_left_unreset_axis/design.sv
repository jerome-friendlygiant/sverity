// Adapted from alexforencich/verilog-axis rtl/axis_fifo.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
// Copyright (c) 2013-2023 Alex Forencich
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software.
// SPDX-License-Identifier: MIT
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_wr,
  input  logic clk_rd,
  input  logic rst_wr_ni,
  input  logic rst_rd_ni,
  input  logic push_i,
  input  logic [7:0] data_i,
  output logic [7:0] data_o
);
  logic write_toggle_q;
  logic [1:0] write_toggle_sync_q;
  logic read_toggle_q;

  always_ff @(posedge clk_wr or negedge rst_wr_ni) begin
    if (!rst_wr_ni)
      write_toggle_q <= 1'b0;
    else if (push_i)
      write_toggle_q <= ~write_toggle_q;
  end

  always_ff @(posedge clk_rd or negedge rst_rd_ni) begin
    if (!rst_rd_ni)
      write_toggle_sync_q <= 2'b00;
    else
      write_toggle_sync_q <= {write_toggle_sync_q[0], write_toggle_q};
  end

  always_ff @(posedge clk_rd) begin
    if (write_toggle_sync_q[1] != read_toggle_q) begin
      data_o <= data_i;
      read_toggle_q <= write_toggle_sync_q[1];
    end
  end
endmodule
