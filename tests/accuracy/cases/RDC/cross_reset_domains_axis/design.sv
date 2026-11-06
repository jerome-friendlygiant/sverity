// Adapted from alexforencich/verilog-axis rtl/sync_reset.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
// Copyright (c) 2014-2020 Alex Forencich
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software.
// SPDX-License-Identifier: MIT
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk,
  input  logic rst_sync,
  input  logic rst_state,
  input  logic event_i,
  output logic state_o
);
  logic [1:0] sync_q;

  always_ff @(posedge clk or posedge rst_sync) begin
    if (rst_sync)
      sync_q <= 2'b11;
    else
      sync_q <= {sync_q[0], 1'b0};
  end

  always_ff @(posedge clk or posedge rst_state) begin
    if (rst_state)
      state_o <= 1'b0;
    else if (!sync_q[1])
      state_o <= event_i;
  end
endmodule
