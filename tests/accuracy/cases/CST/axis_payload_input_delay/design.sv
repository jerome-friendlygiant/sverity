// Adapted from alexforencich/verilog-axis rtl/axis_pipeline_register.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce
// Copyright (c) 2018 Alex Forencich
// Licensed under the MIT license (MIT).
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk,
  input  logic       rst,
  input  logic [15:0] payload_i,
  input  logic       valid_i,
  output logic [15:0] payload_o,
  output logic       valid_o
);
  logic [15:0] payload_q;

  always_ff @(posedge clk) begin
    if (rst) begin
      payload_q <= 16'b0;
      payload_o <= 16'b0;
      valid_o   <= 1'b0;
    end else begin
      payload_q <= payload_i;
      payload_o <= payload_q;
      valid_o   <= valid_i;
    end
  end
endmodule
