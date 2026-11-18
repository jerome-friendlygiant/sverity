// Adapted from common_cells src/cc_credit_counter.sv
// (commit e73baaec2ca665cd80c3c384e9258e35242b829c).
// Copyright 2020 ETH Zurich and University of Bologna.
// Licensed under the Solderpad Hardware License, Version 0.51 (SHL-0.51).

module top #(
  parameter int unsigned NumCredits      = 8,
  parameter bit          InitCreditEmpty = 1'b0,
  localparam int unsigned InitNumCredits  = InitCreditEmpty ? '0 : NumCredits,
  localparam type         credit_cnt_t    = logic [$clog2(NumCredits):0]
) (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic clr_i,

  output credit_cnt_t credit_o,

  input  logic credit_give_i,
  input  logic credit_take_i,

  output logic credit_left_o,
  output logic credit_crit_o,
  output logic credit_full_o
);

  credit_cnt_t credit_d, credit_q;
  logic increment, decrement;

  assign decrement = credit_take_i & ~credit_give_i;
  assign increment = ~credit_take_i & credit_give_i;

  always_comb begin
    credit_d = credit_q;
    if      (decrement) credit_d = credit_q - 1'b1;
    else if (increment) credit_d = credit_q + 1'b1;
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)    credit_q <= credit_cnt_t'(InitNumCredits);
    else if (clr_i) credit_q <= credit_cnt_t'(InitNumCredits);
    else            credit_q <= credit_d;
  end

  assign credit_o       = credit_q;
  assign credit_left_o  = (credit_q >= '0);
  assign credit_crit_o  = (credit_q == NumCredits-1);
  assign credit_full_o  = (credit_q == NumCredits);

endmodule
