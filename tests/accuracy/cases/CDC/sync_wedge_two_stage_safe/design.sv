// Adapted from pulp-platform/common_cells src/cc_sync_wedge.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License").
// Source licence: SHL-0.51.
// Reduced and modified for the SVerity golden set.

module top #(
  parameter int unsigned Stages = 2
) (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic serial_i,
  output logic r_edge_o,
  output logic f_edge_o,
  output logic serial_o
);

  (* async_reg = "true" *) logic sync_meta;
  (* async_reg = "true" *) logic sync_level;
  logic serial_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      sync_meta  <= 1'b0;
      sync_level <= 1'b0;
    end else begin
      sync_meta  <= serial_i;
      sync_level <= sync_meta;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      serial_q <= 1'b0;
    end else begin
      serial_q <= sync_level;
    end
  end

  assign serial_o = serial_q;
  assign f_edge_o = (~sync_level) & serial_q;
  assign r_edge_o = sync_level & (~serial_q);

endmodule
