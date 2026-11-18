// Adapted from common_cells src/cc_heaviside.sv (e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2025 ETH Zurich and University of Bologna. Solderpad Hardware License, Version 0.51. SPDX-License-Identifier: SHL-0.51
module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       load_i,
  input  logic [2:0] level_i,
  output logic [7:0] mask_o,
  output logic [3:0] lanes_o,
  output logic       all_on_o
);
  logic [7:0] mask_d;
  logic [7:0] mask_q;
  logic [3:0] lanes_q;

  // lanes 0..level_i are enabled
  assign mask_d = (8'd1 << (level_i + 3'd1)) - 8'd1;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      mask_q  <= '0;
      lanes_q <= '0;
    end else if (load_i) begin
      mask_q  <= mask_d;
      lanes_q <= level_i + 4'd1;
    end
  end

  assign mask_o   = mask_q;
  assign lanes_o  = lanes_q;
  assign all_on_o = &mask_q;
endmodule
