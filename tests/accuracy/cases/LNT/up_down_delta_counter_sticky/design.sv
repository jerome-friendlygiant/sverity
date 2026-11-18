// Adapted from common_cells src/cc_delta_counter.sv (e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2018 ETH Zurich and University of Bologna. Solderpad Hardware License, Version 0.51. SPDX-License-Identifier: SHL-0.51
module top #(
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
  output logic             wrap_o,
  output logic             overflow_o
);
  logic [Width:0] counter_q, counter_d;
  logic           overflow_d, overflow_q;
  logic           overflow_clr;

  assign overflow_clr = clr_i || load_i;

  always_comb begin
    overflow_d = overflow_q;
    if (!overflow_q && en_i) begin
      if (down_i) begin
        overflow_d = delta_i > counter_q[Width-1:0];
      end else begin
        overflow_d = counter_q[Width-1:0] > ({Width{1'b1}} - delta_i);
      end
    end
  end

  always_comb begin
    counter_d = counter_q;
    if (load_i) begin
      counter_d = {1'b0, d_i};
    end else if (en_i) begin
      if (down_i) begin
        counter_d = counter_q - delta_i;
      end else begin
        counter_d = counter_q + delta_i;
      end
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      counter_q  <= '0;
      overflow_q <= 1'b0;
    end else begin
      counter_q  <= clr_i ? '0 : counter_d;
      overflow_q <= overflow_clr ? 1'b0 : overflow_d;
    end
  end

  assign q_o        = counter_q[Width-1:0];
  assign wrap_o     = counter_q[Width];
  assign overflow_o = overflow_q;
endmodule
