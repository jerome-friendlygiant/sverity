// Adapted from verilog-axis rtl/sync_reset.v
// (commit 48ff7a7e2ef782cf778d47910cf85835c64b1bce).
// Copyright (c) 2014-2020 Alex Forencich
// Licensed under the MIT License.
// SPDX-License-Identifier: MIT

module top #(
  parameter int N       = 2,
  parameter int MaxHold = 15
) (
  input  logic clk,
  input  logic rst,
  input  logic hold_clear_i,
  output logic out,
  output logic timeout_o
);

  logic [N-1:0] sync_reg;
  logic [$clog2(MaxHold+1)-1:0] hold_cnt;
  logic arst;

  assign timeout_o = (hold_cnt == MaxHold);
  assign arst      = rst | timeout_o;
  assign out       = sync_reg[N-1];

  always_ff @(posedge clk or posedge rst) begin
    if (rst) begin
      hold_cnt <= '0;
    end else if (hold_clear_i) begin
      hold_cnt <= '0;
    end else if (!timeout_o) begin
      hold_cnt <= hold_cnt + 1'b1;
    end
  end

  always_ff @(posedge clk or posedge arst) begin
    if (arst) begin
      sync_reg <= {N{1'b1}};
    end else begin
      sync_reg <= {sync_reg[N-2:0], 1'b0};
    end
  end

endmodule
