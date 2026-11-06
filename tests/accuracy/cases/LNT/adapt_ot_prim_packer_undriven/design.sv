// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Adapted from hw/ip/prim/rtl/prim_packer.sv. @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic       valid_i,
  input  logic [7:0] data_i,
  input  logic       ready_i,
  output logic       ready_o,
  output logic       valid_o,
  output logic [7:0] data_o
);
  always_comb begin
    ready_o = ready_i || !valid_i;
    valid_o = valid_i;
  end
endmodule
