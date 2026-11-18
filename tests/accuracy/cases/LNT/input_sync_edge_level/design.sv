// Adapted from common_cells src/cc_edge_detect.sv (e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2018 ETH Zurich and University of Bologna. Solderpad Hardware License, Version 0.51. SPDX-License-Identifier: SHL-0.51
module sync_wedge (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic serial_i,
  output logic r_edge_o,
  output logic f_edge_o,
  output logic serial_o
);
  logic [2:0] sync_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) sync_q <= '0;
    else         sync_q <= {sync_q[1:0], serial_i};
  end

  assign serial_o = sync_q[1];
  assign r_edge_o = sync_q[1] & ~sync_q[2];
  assign f_edge_o = ~sync_q[1] & sync_q[2];
endmodule

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       btn_i,
  output logic       press_o,
  output logic       rel_o,
  output logic [3:0] press_cnt_o,
  output logic       level_o
);
  logic       press;
  logic       rel;
  logic [3:0] cnt_q;
  logic level;
  sync_wedge u_sync (
    .clk_i,
    .rst_ni,
    .serial_i (btn_i),
    .r_edge_o (press),
    .f_edge_o (rel),
    .serial_o (level)
  );

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)     cnt_q <= '0;
    else if (press)  cnt_q <= cnt_q + 4'd1;
  end

  assign press_o     = press;
  assign rel_o       = rel;
  assign press_cnt_o = cnt_q;
  assign level_o = level;
endmodule
