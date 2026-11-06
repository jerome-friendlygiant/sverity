// Adapted from alexforencich/verilog-axis rtl/sync_reset.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
// Copyright (c) 2014-2020 Alex Forencich
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software.
// SPDX-License-Identifier: MIT
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk,
  input  logic rst,
  input  logic flush,
  input  logic valid_i,
  output logic valid_o
);
  logic local_rst;
  assign local_rst = rst | flush;

  always_ff @(posedge clk or posedge local_rst) begin
    if (local_rst)
      valid_o <= 1'b0;
    else
      valid_o <= valid_i;
  end
endmodule
