// Adapted from common_cells src/cc_shift_register.sv (e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2018 ETH Zurich and University of Bologna. Solderpad Hardware License, Version 0.51. SPDX-License-Identifier: SHL-0.51
module shift_register_gated #(
  parameter int unsigned Depth = 4,
  parameter int unsigned Width = 8
) (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic             clr_i,
  input  logic             valid_i,
  input  logic [Width-1:0] data_i,
  output logic             valid_o,
  output logic [Width-1:0] data_o
);
  logic [Width-1:0] data_q [Depth];
  logic [Depth-1:0] vld_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      for (int unsigned i = 0; i < Depth; i++) data_q[i] <= '0;
      vld_q <= '0;
    end else if (clr_i) begin
      vld_q <= '0;
    end else if (valid_i) begin
      data_q[0] <= data_i;
      vld_q[0]  <= 1'b1;
      for (int unsigned i = 1; i < Depth; i++) begin
        data_q[i] <= data_q[i-1];
        vld_q[i]  <= vld_q[i-1];
      end
    end
  end

  assign data_o  = data_q[Depth-1];
  assign valid_o = vld_q[Depth-1];
endmodule

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       flush_i,
  input  logic [7:0] d_i,
  output logic [7:0] d_o,
  output logic       primed_o
);
  shift_register_gated #(.Depth(6), .Width(8)) i_delay (
    .clk_i,
    .rst_ni,
    .clr_i   (flush_i),
    .valid_i (1'b1),
    .data_i  (d_i),
    .valid_o (primed_o),
    .data_o  (d_o)
  );
endmodule
