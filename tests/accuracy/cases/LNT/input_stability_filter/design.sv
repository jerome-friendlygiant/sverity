// Adapted from OpenTitan hw/ip/prim/rtl/prim_filter.sv
// (commit 92f3d03b0dcf6d5c1933a65d1497747f67d19f4c).
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

module top #(
  parameter int unsigned Cycles = 4
) (
  input        clk_i,
  input        rst_ni,
  input        enable_i,
  input        filter_i,
  output logic filter_o
);

  logic [Cycles-2:0] stored_vector_q;
  logic [Cycles-1:0] window;
  logic stored_value_q, update_stored_value;

  assign window = {stored_vector_q, filter_i};

  always_ff @(posedge clk_i) begin
    if (update_stored_value) begin
      stored_value_q <= filter_i;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      stored_vector_q <= '0;
    end else begin
      stored_vector_q <= window[Cycles-2:0];
    end
  end

  assign update_stored_value =
             (window == {Cycles{1'b0}}) |
             (window == {Cycles{1'b1}});

  assign filter_o = enable_i ? stored_value_q : filter_i;

endmodule
