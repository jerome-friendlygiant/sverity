// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_max_tree.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top #(
  parameter int unsigned NumSrc = 4,
  parameter int unsigned Width = 8,
  localparam int unsigned SrcWidth = NumSrc > 1 ? $clog2(NumSrc) : 1
) (
  input  logic [NumSrc-1:0][Width-1:0] values_i,
  input  logic [NumSrc-1:0]            valid_i,
  output logic [Width-1:0]             max_value_o,
  output logic [SrcWidth-1:0]          max_idx_o,
  output logic                         max_valid_o
);
  always_comb begin
    max_value_o = values_i[0];
    max_idx_o = '0;
    max_valid_o = 1'b0;
    for (int i = 0; i < NumSrc; i++) begin
      if (valid_i[i] && (!max_valid_o || values_i[i] > max_value_o)) begin
        max_value_o = values_i[i];
        max_idx_o = SrcWidth'(i);
        max_valid_o = 1'b1;
      end
    end
  end
endmodule
