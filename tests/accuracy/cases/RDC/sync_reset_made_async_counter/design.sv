// Adapted from lowRISC/ibex rtl/ibex_counter.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_count,
  input  logic clk_report,
  input  logic clear_count_i,
  input  logic increment_i,
  output logic [7:0] count_o
);
  logic clear_count_q;
  logic report_rst_ni;

  always_ff @(posedge clk_count) begin
    if (clear_count_i)
      clear_count_q <= 1'b1;
    else
      clear_count_q <= 1'b0;
  end

  assign report_rst_ni = ~clear_count_q;

  always_ff @(posedge clk_report or negedge report_rst_ni) begin
    if (!report_rst_ni)
      count_o <= 8'h00;
    else if (increment_i)
      count_o <= count_o + 8'h01;
  end
endmodule
