// Adapted from common_cells src/cc_exp_backoff.sv
// (commit e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2019 ETH Zurich and University of Bologna.
// Licensed under the Solderpad Hardware License, Version 0.51 (SHL-0.51).

module top #(
  parameter int unsigned Seed   = 'hffff,
  parameter int unsigned MaxExp = 8
) (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic clr_i,
  input  logic set_cnt_i,
  input  logic clr_cnt_i,
  output logic is_zero_o
);

  localparam int unsigned WIDTH = 16;

  logic [WIDTH-1:0] lfsr_d, lfsr_q, cnt_d, cnt_q, mask_d, mask_q;
  logic lfsr;

  assign lfsr = lfsr_q[15-15] ^
                lfsr_q[15-13] ^
                lfsr_q[15-12] ^
                lfsr_q[15-10];

  assign lfsr_d = (set_cnt_i) ? {lfsr, lfsr_q[$high(lfsr_q):1]} :
                                lfsr_q;

  assign mask_d = (clr_cnt_i) ? '0 :
                  (set_cnt_i) ? {{(WIDTH-MaxExp){1'b0}}, mask_q[MaxExp-2:0], 1'b1} :
                                mask_q;

  assign cnt_d = (clr_cnt_i) ? '0 :
                 (set_cnt_i) ? (mask_q & lfsr_q) :
                 (!is_zero_o) ? cnt_q - 1'b1 : '0;

  assign is_zero_o = (cnt_q == '0);

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      lfsr_q <= WIDTH'(Seed);
      mask_q <= '0;
      cnt_q  <= '0;
    end else if (clr_i) begin
      lfsr_q <= WIDTH'(Seed);
      mask_q <= '0;
      cnt_q  <= '0;
    end else begin
      lfsr_q <= lfsr_d;
      mask_q <= mask_d;
      cnt_q   = cnt_d;
    end
  end

endmodule
