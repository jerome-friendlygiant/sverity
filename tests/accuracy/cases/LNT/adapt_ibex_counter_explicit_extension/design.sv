// Adapted from lowRISC/ibex rtl/ibex_counter.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top #(
  parameter int unsigned CounterWidth = 8
) (
  input  logic                   clk_i,
  input  logic                   rst_ni,
  input  logic                   inc_i,
  input  logic                   load_i,
  input  logic [CounterWidth-1:0] load_value_i,
  output logic [15:0]            value_o,
  output logic [15:0]            updated_o
);
  logic [CounterWidth-1:0] counter_q, counter_d;
  logic [CounterWidth:0] counter_upd;

  // The extra bit is intentional: the stored counter wraps only after the
  // complete addition has been formed.
  assign counter_upd = {1'b0, counter_q} + {{CounterWidth{1'b0}}, 1'b1};

  always_comb begin
    counter_d = counter_q;
    if (load_i) counter_d = load_value_i;
    else if (inc_i) counter_d = counter_upd[CounterWidth-1:0];
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) counter_q <= '0;
    else counter_q <= counter_d;
  end

  assign value_o = 16'(counter_q);
  assign updated_o = 16'(counter_upd[CounterWidth-1:0]);
endmodule
