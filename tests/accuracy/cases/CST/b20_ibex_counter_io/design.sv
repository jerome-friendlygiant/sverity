// Adapted from lowRISC/ibex rtl/ibex_counter.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Copyright lowRISC contributors. Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        counter_inc_i,
  input  logic        counterh_we_i,
  input  logic        counter_we_i,
  input  logic [31:0] counter_val_i,
  output logic [63:0] counter_val_o,
  output logic [63:0] counter_val_upd_o
);
  logic [63:0] counter_q;
  logic [63:0] load_value;

  always_comb begin
    load_value = counter_q;
    if (counter_we_i)  load_value[31:0]  = counter_val_i;
    if (counterh_we_i) load_value[63:32] = counter_val_i;
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) counter_q <= '0;
    else if (counter_we_i || counterh_we_i) counter_q <= load_value;
    else if (counter_inc_i)                 counter_q <= counter_q + 64'd1;
  end

  assign counter_val_o     = counter_q;
  assign counter_val_upd_o = counter_q + 64'd1;
endmodule
