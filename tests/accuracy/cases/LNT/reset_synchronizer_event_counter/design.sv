// Adapted from verilog-axis rtl/sync_reset.v
// (commit 48ff7a7e2ef782cf778d47910cf85835c64b1bce).
// Copyright (c) 2014-2020 Alex Forencich
// Licensed under the MIT License.
// SPDX-License-Identifier: MIT

module top #(
  parameter int N     = 2,
  parameter int Width = 12
) (
  input  logic             clk,
  input  logic             rst,
  input  logic             event_i,
  output logic [Width-1:0] event_count_o,
  output logic             in_reset_o
);

  logic [N-1:0] sync_reg;
  logic         rst_sync;
  logic [Width-1:0] count_q;

  assign rst_sync = sync_reg[N-1];

  always_ff @(posedge clk or posedge rst) begin
    if (rst) begin
      sync_reg <= {N{1'b1}};
    end else begin
      sync_reg <= {sync_reg[N-2:0], 1'b0};
    end
  end

  always_ff @(posedge clk or posedge rst_sync) begin
    if (rst_sync) begin
      count_q <= '0;
    end else if (event_i) begin
      count_q <= count_q + 1'b1;
    end
  end

  assign event_count_o = count_q;
  assign in_reset_o    = rst_sync;

endmodule
