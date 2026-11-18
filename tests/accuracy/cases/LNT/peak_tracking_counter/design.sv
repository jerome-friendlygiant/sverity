// Adapted from common_cells src/cc_max_counter.sv (e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2018 ETH Zurich and University of Bologna. Solderpad Hardware License, Version 0.51. SPDX-License-Identifier: SHL-0.51
module step_counter #(
  parameter int unsigned Width = 8
) (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic             clr_i,
  input  logic             en_i,
  input  logic             load_i,
  input  logic             down_i,
  input  logic [Width-1:0] delta_i,
  input  logic [Width-1:0] d_i,
  output logic [Width-1:0] q_o,
  output logic             overflow_o
);
  logic [Width:0] cnt_q, cnt_d;

  always_comb begin
    cnt_d = cnt_q;
    if (load_i) begin
      cnt_d = {1'b0, d_i};
    end else if (en_i) begin
      cnt_d = down_i ? cnt_q - delta_i : cnt_q + delta_i;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)    cnt_q <= '0;
    else if (clr_i) cnt_q <= '0;
    else            cnt_q <= cnt_d;
  end

  assign q_o        = cnt_q[Width-1:0];
  assign overflow_o = cnt_q[Width];
endmodule

module top (
  input  logic       clk_i,
  input  logic       rst_ni,
  input  logic       clr_i,
  input  logic       clr_max_i,
  input  logic       en_i,
  input  logic       load_i,
  input  logic       down_i,
  input  logic [7:0] delta_i,
  input  logic [7:0] d_i,
  output logic [7:0] q_o,
  output logic [7:0] max_o,
  output logic       overflow_o
);
  logic [7:0] max_q;

  step_counter #(.Width(8)) i_counter (
    .clk_i,
    .rst_ni,
    .clr_i,
    .load_i,
    .down_i,
    .delta_i,
    .d_i,
    .q_o,
    .overflow_o
  );

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)         max_q <= '0;
    else if (clr_max_i)  max_q <= '0;
    else if (en_i && q_o > max_q) max_q <= q_o;
  end

  assign max_o = max_q;
endmodule
