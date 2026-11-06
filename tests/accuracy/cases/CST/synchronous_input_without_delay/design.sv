// Adapted from alexforencich/verilog-axis rtl/axis_rate_limit.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
// Copyright (c) 2014-2018 Alex Forencich
// Licensed under the MIT license (MIT).
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk,
  input  logic       rst,
  input  logic [7:0] data_i,
  input  logic       valid_i,
  output logic [7:0] data_o,
  output logic       valid_o
);
  always_ff @(posedge clk) begin
    if (rst) begin
      data_o  <= 8'b0;
      valid_o <= 1'b0;
    end else begin
      data_o  <= data_i;
      valid_o <= valid_i;
    end
  end
endmodule
