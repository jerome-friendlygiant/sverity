// Adapted from Ibex rtl/ibex_counter.sv
// (commit 2373e5dadcfc4f56672962fa242f6a051acc9aca).
// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

module top #(
  parameter int CounterWidth = 32
) (
  input  logic                    clk_i,
  input  logic                    rst_ni,
  input  logic                    counter_inc_i,
  input  logic                    counter_we_i,
  input  logic [CounterWidth-1:0] counter_val_i,
  output logic [CounterWidth-1:0] counter_val_o,
  output logic [CounterWidth-1:0] counter_val_upd_o
);

  logic [CounterWidth-1:0] counter_upd;
  logic [CounterWidth-1:0] counter_d;
  logic [CounterWidth-1:0] counter_q;

  localparam bit UseSyncReset = 1'b0;

  assign counter_upd = counter_q + {{CounterWidth - 1{1'b0}}, 1'b1};

  always_comb begin
    if (counter_we_i) begin
      counter_d = counter_val_i;
    end else if (counter_inc_i) begin
      counter_d = counter_upd;
    end else begin
      counter_d = counter_q;
    end
  end

  if (UseSyncReset) begin : g_cnt_sync_rst
    always_ff @(posedge clk_i) begin
      if (!rst_ni) begin
        counter_q <= '0;
      end else begin
        counter_q <= counter_d;
      end
    end
  end else begin : g_cnt_async_rst
    always_ff @(posedge clk_i or negedge rst_ni) begin
      if (!rst_ni) begin
        counter_q <= '0;
      end else begin
        counter_q <= counter_d;
      end
    end
  end

  assign counter_val_o     = counter_q;
  assign counter_val_upd_o = counter_upd;

endmodule
