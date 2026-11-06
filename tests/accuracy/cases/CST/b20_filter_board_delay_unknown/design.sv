// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_filter.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project). Licensed under the Apache License, Version 2.0. SPDX-License-Identifier: Apache-2.0.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic enable_i,
  input  logic filter_i,
  output logic filter_o
);
  logic [2:0] history_q;
  logic stored_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      history_q <= '0;
      stored_q  <= 1'b0;
    end else begin
      history_q <= {history_q[1:0], filter_i};
      if (({history_q[1:0], filter_i} == 3'b000) ||
          ({history_q[1:0], filter_i} == 3'b111)) begin
        stored_q <= filter_i;
      end
    end
  end

  assign filter_o = enable_i ? stored_q : filter_i;
endmodule
