// Adapted from common_cells src/cc_serial_deglitch.sv
// (commit e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2026 ETH Zurich and University of Bologna.
// Licensed under the Solderpad Hardware License, Version 0.51 (SHL-0.51).

module top #(
  parameter int unsigned Threshold = 4
) (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic clr_i,
  input  logic restart_i,
  input  logic en_i,
  input  logic d_i,
  output logic q_o
);

  localparam int unsigned CntWidth = (Threshold + 1 > 1) ? $clog2(Threshold + 1) : 1;

  logic [CntWidth-1:0] count_q, count_d;
  logic                mismatch, stable_edge;
  logic                count_load, count_clear;
  logic                q_d;

  assign mismatch = (d_i != q_o);
  assign count_d  = count_q + CntWidth'(1);

  assign stable_edge = en_i && mismatch && (count_d == CntWidth'(Threshold));
  assign count_clear = clr_i || restart_i || (en_i && !mismatch) || stable_edge;
  assign count_load  = en_i && mismatch && !stable_edge;

  assign q_d = (restart_i || stable_edge) ? d_i : q_o;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      count_q <= '0;
    end else if (count_clear) begin
      count_q <= '0;
    end else if (count_load) begin
      count_q <= count_d;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      q_o <= d_i;
    end else if (clr_i) begin
      q_o <= 1'b0;
    end else begin
      q_o <= q_d;
    end
  end

endmodule
