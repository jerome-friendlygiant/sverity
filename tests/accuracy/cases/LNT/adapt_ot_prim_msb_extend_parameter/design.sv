// Adapted from lowRISC/opentitan hw/ip/prim/rtl/prim_msb_extend.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top #(
  parameter int unsigned InWidth = 4,
  parameter int unsigned OutWidth = 8,
  localparam int unsigned WidthDiff = OutWidth - InWidth
) (
  input  logic [InWidth-1:0]  in_i,
  output logic [OutWidth-1:0] out_o
);
  initial begin
    if (OutWidth < InWidth) $error("OutWidth must not be smaller than InWidth");
  end

  if (WidthDiff == 0) begin : gen_feedthru
    assign out_o = in_i;
  end else begin : gen_sign_extend
    assign out_o = {{WidthDiff{in_i[InWidth-1]}}, in_i};
  end
endmodule
